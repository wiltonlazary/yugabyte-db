// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/docdb/intent_aware_iterator.h"

#include <future>
#include <thread>
#include <boost/optional/optional_io.hpp>

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/docdb/conflict_resolution.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/intent.h"
#include "yb/docdb/value.h"

#include "yb/server/hybrid_clock.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/bytes_formatter.h"

using namespace std::literals;

DEFINE_bool(transaction_allow_rerequest_status_in_tests, true,
            "Allow rerequest transaction status when try again is received.");

namespace yb {
namespace docdb {

namespace {

void GetIntentPrefixForKeyWithoutHt(const Slice& key, KeyBytes* out) {
  out->Clear();
  // Since caller guarantees that key_bytes doesn't have hybrid time, we can simply use it
  // to get prefix for all related intents.
  out->AppendRawBytes(key);
}

KeyBytes GetIntentPrefixForKeyWithoutHt(const Slice& key) {
  KeyBytes result;
  GetIntentPrefixForKeyWithoutHt(key, &result);
  return result;
}

void AppendEncodedDocHt(const Slice& encoded_doc_ht, KeyBytes* key_bytes) {
  key_bytes->AppendValueType(ValueType::kHybridTime);
  key_bytes->AppendRawBytes(encoded_doc_ht);
}

} // namespace

// For locally committed transactions returns commit time if committed at specified time or
// HybridTime::kMin otherwise. For other transactions returns HybridTime::kInvalid.
HybridTime TransactionStatusCache::GetLocalCommitTime(const TransactionId& transaction_id) {
  const HybridTime local_commit_time = txn_status_manager_->LocalCommitTime(transaction_id);
  return local_commit_time.is_valid()
      ? local_commit_time <= read_time_.global_limit ? local_commit_time : HybridTime::kMin
      : local_commit_time;
}

Result<HybridTime> TransactionStatusCache::GetCommitTime(const TransactionId& transaction_id) {
  auto it = cache_.find(transaction_id);
  if (it != cache_.end()) {
    return it->second;
  }

  auto result = DoGetCommitTime(transaction_id);
  if (result.ok()) {
    cache_.emplace(transaction_id, *result);
  }
  return result;
}

Result<HybridTime> TransactionStatusCache::DoGetCommitTime(const TransactionId& transaction_id) {
  HybridTime local_commit_time = GetLocalCommitTime(transaction_id);
  if (local_commit_time.is_valid()) {
    return local_commit_time;
  }

  // Since TransactionStatusResult does not have default ctor we should init it somehow.
  TransactionStatusResult txn_status(TransactionStatus::ABORTED, HybridTime());
  CoarseBackoffWaiter waiter(deadline_, 50ms /* max_wait */);
  static const std::string kRequestReason = "get commit time"s;
  for(;;) {
    std::promise<Result<TransactionStatusResult>> txn_status_promise;
    auto future = txn_status_promise.get_future();
    auto callback = [&txn_status_promise](Result<TransactionStatusResult> result) {
      txn_status_promise.set_value(std::move(result));
    };
    txn_status_manager_->RequestStatusAt(
        {&transaction_id, read_time_.read, read_time_.global_limit, read_time_.serial_no,
              &kRequestReason,
              TransactionLoadFlags{TransactionLoadFlag::kMustExist, TransactionLoadFlag::kCleanup},
              callback});
    future.wait();
    auto txn_status_result = future.get();
    if (txn_status_result.ok()) {
      txn_status = std::move(*txn_status_result);
      break;
    }
    if (txn_status_result.status().IsNotFound()) {
      // We have intent w/o metadata, that means that transaction was already cleaned up.
      LOG(WARNING) << "Intent for transaction w/o metadata: " << transaction_id;
      return HybridTime::kMin;
    }
    LOG(WARNING)
        << "Failed to request transaction " << yb::ToString(transaction_id) << " status: "
        <<  txn_status_result.status();
    if (!txn_status_result.status().IsTryAgain()) {
      return std::move(txn_status_result.status());
    }
    DCHECK(FLAGS_transaction_allow_rerequest_status_in_tests);
    if (!waiter.Wait()) {
      return STATUS(TimedOut, "");
    }
  }
  VLOG(4) << "Transaction_id " << transaction_id << " at " << read_time_
          << ": status: " << TransactionStatus_Name(txn_status.status)
          << ", status_time: " << txn_status.status_time;
  // There could be case when transaction was committed and applied between previous call to
  // GetLocalCommitTime, in this case coordinator does not know transaction and will respond
  // with ABORTED status. So we recheck whether it was committed locally.
  if (txn_status.status == TransactionStatus::ABORTED) {
    local_commit_time = GetLocalCommitTime(transaction_id);
    return local_commit_time.is_valid() ? local_commit_time : HybridTime::kMin;
  } else {
    return txn_status.status == TransactionStatus::COMMITTED ? txn_status.status_time
        : HybridTime::kMin;
  }
}

namespace {

struct DecodeStrongWriteIntentResult {
  Slice intent_prefix;
  Slice intent_value;
  DocHybridTime value_time;
  IntentTypeSet intent_types;

  // Whether this intent from the same transaction as specified in context.
  bool same_transaction = false;

  std::string ToString() const {
    return Format("{ intent_prefix: $0 intent_value: $1 value_time: $2 same_transaction: $3 "
                  "intent_types: $4 }",
                  intent_prefix.ToDebugHexString(), intent_value.ToDebugHexString(), value_time,
                  same_transaction, intent_types);
  }
};

std::ostream& operator<<(std::ostream& out, const DecodeStrongWriteIntentResult& result) {
  return out << result.ToString();
}

// Decodes intent based on intent_iterator and its transaction commit time if intent is a strong
// write intent, intent is not for row locking, and transaction is already committed at specified
// time or is current transaction.
// Returns HybridTime::kMin as value_time otherwise.
// For current transaction returns intent record hybrid time as value_time.
// Consumes intent from value_slice leaving only value itself.
Result<DecodeStrongWriteIntentResult> DecodeStrongWriteIntent(
    TransactionOperationContext txn_op_context, rocksdb::Iterator* intent_iter,
    TransactionStatusCache* transaction_status_cache) {
  DecodeStrongWriteIntentResult result;
  auto decoded_intent_key = VERIFY_RESULT(DecodeIntentKey(intent_iter->key()));
  result.intent_prefix = decoded_intent_key.intent_prefix;
  result.intent_types = decoded_intent_key.intent_types;
  if (result.intent_types.Test(IntentType::kStrongWrite)) {
    result.intent_value = intent_iter->value();
    auto txn_id = VERIFY_RESULT(DecodeTransactionIdFromIntentValue(&result.intent_value));
    result.same_transaction = txn_id == txn_op_context.transaction_id;
    if (result.intent_value.size() < 1 + sizeof(IntraTxnWriteId) ||
        result.intent_value[0] != ValueTypeAsChar::kWriteId) {
      return STATUS_FORMAT(
          Corruption, "Write id is missing in $0", intent_iter->value().ToDebugHexString());
    }
    result.intent_value.consume_byte();
    IntraTxnWriteId in_txn_write_id = BigEndian::Load32(result.intent_value.data());
    result.intent_value.remove_prefix(sizeof(IntraTxnWriteId));
    if (result.intent_value.starts_with(ValueTypeAsChar::kRowLock)) {
      result.value_time = DocHybridTime::kMin;
    } else if (result.same_transaction) {
      result.value_time = decoded_intent_key.doc_ht;
    } else {
      auto commit_ht = VERIFY_RESULT(transaction_status_cache->GetCommitTime(txn_id));
      result.value_time = DocHybridTime(
          commit_ht, commit_ht != HybridTime::kMin ? in_txn_write_id : 0);
      VLOG(4) << "Transaction id: " << txn_id << ", value time: " << result.value_time
              << ", value: " << result.intent_value.ToDebugHexString();
    }
  } else {
    result.value_time = DocHybridTime::kMin;
  }
  return result;
}

// Given that key is well-formed DocDB encoded key, checks if it is an intent key for the same key
// as intent_prefix. If key is not well-formed DocDB encoded key, result could be true or false.
bool IsIntentForTheSameKey(const Slice& key, const Slice& intent_prefix) {
  return key.starts_with(intent_prefix) &&
         key.size() > intent_prefix.size() &&
         IntentValueType(key[intent_prefix.size()]);
}

std::string DebugDumpKeyToStr(const Slice &key) {
  return key.ToDebugString() + " " + key.ToDebugHexString() + " (" +
      SubDocKey::DebugSliceToString(key) + ")";
}

std::string DebugDumpKeyToStr(const KeyBytes &key) {
  return DebugDumpKeyToStr(key.AsSlice());
}

bool DebugHasHybridTime(const Slice& subdoc_key_encoded) {
  SubDocKey subdoc_key;
  CHECK(subdoc_key.FullyDecodeFromKeyWithOptionalHybridTime(subdoc_key_encoded).ok());
  return subdoc_key.has_hybrid_time();
}

} // namespace

IntentAwareIterator::IntentAwareIterator(
    const DocDB& doc_db,
    const rocksdb::ReadOptions& read_opts,
    CoarseTimePoint deadline,
    const ReadHybridTime& read_time,
    const TransactionOperationContextOpt& txn_op_context)
    : read_time_(read_time),
      encoded_read_time_local_limit_(
          DocHybridTime(read_time_.local_limit, kMaxWriteId).EncodedInDocDbFormat()),
      encoded_read_time_global_limit_(
          DocHybridTime(read_time_.global_limit, kMaxWriteId).EncodedInDocDbFormat()),
      txn_op_context_(txn_op_context),
      transaction_status_cache_(
          txn_op_context ? &txn_op_context->txn_status_manager : nullptr, read_time, deadline) {
  VLOG(4) << "IntentAwareIterator, read_time: " << read_time
          << ", txn_op_context: " << txn_op_context_;

  if (txn_op_context.is_initialized()) {
    intent_iter_ = docdb::CreateRocksDBIterator(doc_db.intents,
                                                doc_db.key_bounds,
                                                docdb::BloomFilterMode::DONT_USE_BLOOM_FILTER,
                                                boost::none,
                                                rocksdb::kDefaultQueryId,
                                                nullptr /* file_filter */,
                                                &intent_upperbound_);
  }
  // WARNING: Is is important for regular DB iterator to be created after intents DB iterator,
  // otherwise consistency could break, for example in following scenario:
  // 1) Transaction is T1 committed with value v1 for k1, but not yet applied to regular DB.
  // 2) Client reads v1 for k1.
  // 3) Regular DB iterator is created on a regular DB snapshot containing no values for k1.
  // 4) Transaction T1 is applied, k1->v1 is written into regular DB, intent k1->v1 is deleted.
  // 5) Intents DB iterator is created on an intents DB snapshot containing no intents for k1.
  // 6) Client reads no values for k1.
  iter_ = BoundedRocksDbIterator(doc_db.regular, read_opts, doc_db.key_bounds);
}

void IntentAwareIterator::Seek(const DocKey &doc_key) {
  Seek(doc_key.Encode());
}

void IntentAwareIterator::Seek(const Slice& key) {
  VLOG(4) << "Seek(" << SubDocKey::DebugSliceToString(key) << ")";
  DOCDB_DEBUG_SCOPE_LOG(
      key.ToDebugString(),
      std::bind(&IntentAwareIterator::DebugDump, this));
  if (!status_.ok()) {
    return;
  }

  ROCKSDB_SEEK(&iter_, key);
  skip_future_records_needed_ = true;

  if (intent_iter_.Initialized()) {
    seek_intent_iter_needed_ = SeekIntentIterNeeded::kSeek;
    GetIntentPrefixForKeyWithoutHt(key, &seek_key_buffer_);
  }
}

void IntentAwareIterator::SeekForward(const Slice& key) {
  KeyBytes key_bytes;
  // Reserve space for key plus kMaxBytesPerEncodedHybridTime + 1 bytes for SeekForward() below to
  // avoid extra realloc while appending the read time.
  key_bytes.Reserve(key.size() + kMaxBytesPerEncodedHybridTime + 1);
  key_bytes.AppendRawBytes(key);
  SeekForward(&key_bytes);
}

void IntentAwareIterator::SeekForward(KeyBytes* key_bytes) {
  VLOG(4) << "SeekForward(" << SubDocKey::DebugSliceToString(*key_bytes) << ")";
  DOCDB_DEBUG_SCOPE_LOG(
      SubDocKey::DebugSliceToString(*key_bytes),
      std::bind(&IntentAwareIterator::DebugDump, this));
  if (!status_.ok()) {
    return;
  }

  const size_t key_size = key_bytes->size();
  AppendEncodedDocHt(encoded_read_time_global_limit_, key_bytes);
  SeekForwardRegular(*key_bytes);
  key_bytes->Truncate(key_size);
  if (intent_iter_.Initialized() && status_.ok()) {
    seek_intent_iter_needed_ = SeekIntentIterNeeded::kSeekForward;
    GetIntentPrefixForKeyWithoutHt(*key_bytes, &seek_key_buffer_);
  }
}

// TODO: If TTL rows are ever supported on subkeys, this may need to change appropriately.
// Otherwise, this function might seek past the TTL merge record, but not the original
// record for the actual subkey.
void IntentAwareIterator::SeekPastSubKey(const Slice& key) {
  VLOG(4) << "SeekPastSubKey(" << SubDocKey::DebugSliceToString(key) << ")";
  if (!status_.ok()) {
    return;
  }

  docdb::SeekPastSubKey(key, &iter_);
  skip_future_records_needed_ = true;
  if (intent_iter_.Initialized() && status_.ok()) {
    seek_intent_iter_needed_ = SeekIntentIterNeeded::kSeekForward;
    GetIntentPrefixForKeyWithoutHt(key, &seek_key_buffer_);
    // Skip all intents for subdoc_key.
    seek_key_buffer_.mutable_data()->push_back(ValueTypeAsChar::kObsoleteIntentType + 1);
  }
}

void IntentAwareIterator::SeekOutOfSubDoc(KeyBytes* key_bytes) {
  VLOG(4) << "SeekOutOfSubDoc(" << SubDocKey::DebugSliceToString(*key_bytes) << ")";
  if (!status_.ok()) {
    return;
  }

  docdb::SeekOutOfSubKey(key_bytes, &iter_);
  skip_future_records_needed_ = true;
  if (intent_iter_.Initialized() && status_.ok()) {
    seek_intent_iter_needed_ = SeekIntentIterNeeded::kSeekForward;
    GetIntentPrefixForKeyWithoutHt(*key_bytes, &seek_key_buffer_);
    // See comment for SubDocKey::AdvanceOutOfSubDoc.
    seek_key_buffer_.AppendValueType(ValueType::kMaxByte);
  }
}

void IntentAwareIterator::SeekOutOfSubDoc(const Slice& key) {
  KeyBytes key_bytes;
  // Reserve space for key + 1 byte for docdb::SeekOutOfSubKey() above to avoid extra realloc while
  // appending kMaxByte.
  key_bytes.Reserve(key.size() + 1);
  key_bytes.AppendRawBytes(key);
  SeekOutOfSubDoc(&key_bytes);
}

void IntentAwareIterator::SeekToLastDocKey() {
  iter_.SeekToLast();
  SkipFutureRecords(Direction::kBackward);
  if (intent_iter_.Initialized()) {
    ResetIntentUpperbound();
    intent_iter_.SeekToLast();
    SeekToSuitableIntent<Direction::kBackward>();
    seek_intent_iter_needed_ = SeekIntentIterNeeded::kNoNeed;
    skip_future_intents_needed_ = false;
  }
  if (!iter_valid_ && resolved_intent_state_ != ResolvedIntentState::kValid) {
    return;
  }
  SeekToLatestDocKeyInternal();
}

template <class T>
void Assign(const T& value, T* out) {
  if (out) {
    *out = value;
  }
}

// If we reach a different key, stop seeking.
Status IntentAwareIterator::NextFullValue(
    DocHybridTime* latest_record_ht,
    Slice* result_value,
    Slice* final_key) {
  if (!latest_record_ht || !result_value)
    return STATUS(Corruption, "The arguments latest_record_ht and "
                              "result_value cannot be null pointers.");
  RETURN_NOT_OK(status_);
  Slice v;
  if (!valid() || !IsMergeRecord(v = value())) {
    auto key_data = VERIFY_RESULT(FetchKey());
    Assign(key_data.key, final_key);
    Assign(key_data.write_time, latest_record_ht);
    *result_value = v;
    return status_;
  }

  *latest_record_ht = DocHybridTime::kMin;
  const auto key_data = VERIFY_RESULT(FetchKey());
  auto key = key_data.key;
  const size_t key_size = key.size();
  bool found_record = false;

  // The condition specifies that the first type is the flags type,
  // And that the key is still the same.
  while ((found_record = iter_.Valid() &&
          (key = iter_.key()).starts_with(key_data.key) &&
          (ValueType)(key[key_size]) == ValueType::kHybridTime) &&
         IsMergeRecord(v = iter_.value())) {
    iter_.Next();
  }

  if (found_record) {
    *result_value = v;
    *latest_record_ht = VERIFY_RESULT(DocHybridTime::DecodeFromEnd(&key));
    Assign(key, final_key);
  }

  found_record = false;
  if (intent_iter_.Initialized()) {
    while ((found_record = IsIntentForTheSameKey(intent_iter_.key(), key_data.key)) &&
           IsMergeRecord(v = intent_iter_.value())) {
      intent_iter_.Next();
    }
    DocHybridTime doc_ht;
    if (found_record && !(key = intent_iter_.key()).empty() &&
        (doc_ht = VERIFY_RESULT(DocHybridTime::DecodeFromEnd(&key))) >= *latest_record_ht) {
      *latest_record_ht = doc_ht;
      *result_value = v;
      Assign(key, final_key);
    }
  }

  if (*latest_record_ht == DocHybridTime::kMin) {
    iter_valid_ = false;
  }
  return status_;
}

void IntentAwareIterator::PrevSubDocKey(const KeyBytes& key_bytes) {
  ROCKSDB_SEEK(&iter_, key_bytes);

  if (iter_.Valid()) {
    iter_.Prev();
  } else {
    iter_.SeekToLast();
  }
  SkipFutureRecords(Direction::kBackward);

  if (intent_iter_.Initialized()) {
    ResetIntentUpperbound();
    ROCKSDB_SEEK(&intent_iter_, GetIntentPrefixForKeyWithoutHt(key_bytes));
    if (intent_iter_.Valid()) {
      intent_iter_.Prev();
    } else {
      intent_iter_.SeekToLast();
    }
    SeekToSuitableIntent<Direction::kBackward>();
    seek_intent_iter_needed_ = SeekIntentIterNeeded::kNoNeed;
    skip_future_intents_needed_ = false;
  }

  if (!iter_valid_ && resolved_intent_state_ != ResolvedIntentState::kValid) {
    return;
  }
  SeekToLatestSubDocKeyInternal();
}

void IntentAwareIterator::PrevDocKey(const DocKey& doc_key) {
  PrevDocKey(doc_key.Encode().AsSlice());
}

void IntentAwareIterator::PrevDocKey(const Slice& encoded_doc_key) {
  ROCKSDB_SEEK(&iter_, encoded_doc_key);
  if (iter_.Valid()) {
    iter_.Prev();
  } else {
    iter_.SeekToLast();
  }
  SkipFutureRecords(Direction::kBackward);

  if (intent_iter_.Initialized()) {
    ResetIntentUpperbound();
    ROCKSDB_SEEK(&intent_iter_, GetIntentPrefixForKeyWithoutHt(encoded_doc_key));
    if (intent_iter_.Valid()) {
      intent_iter_.Prev();
    } else {
      intent_iter_.SeekToLast();
    }
    SeekToSuitableIntent<Direction::kBackward>();
    seek_intent_iter_needed_ = SeekIntentIterNeeded::kNoNeed;
    skip_future_intents_needed_ = false;
  }

  if (!iter_valid_ && resolved_intent_state_ != ResolvedIntentState::kValid) {
    return;
  }
  SeekToLatestDocKeyInternal();
}

void IntentAwareIterator::SeekToLatestSubDocKeyInternal() {
  DCHECK(iter_valid_ || resolved_intent_state_ == ResolvedIntentState::kValid)
      << "Expected iter_valid(" << iter_valid_ << ") || resolved_intent_state_("
      << resolved_intent_state_ << ") == ResolvedIntentState::kValid";
  // Choose latest subkey among regular and intent iterators.
  Slice subdockey_slice(
      !iter_valid_ ||
      (resolved_intent_state_ == ResolvedIntentState::kValid
          && iter_.key().compare(resolved_intent_sub_doc_key_encoded_) < 0)
      ? resolved_intent_key_prefix_.AsSlice() : iter_.key());

  // Strip the hybrid time and seek the slice.
  auto doc_ht = DocHybridTime::DecodeFromEnd(&subdockey_slice);
  if (!doc_ht.ok()) {
    status_ = doc_ht.status();
    return;
  }
  subdockey_slice.remove_suffix(1);
  Seek(subdockey_slice);
}

void IntentAwareIterator::SeekToLatestDocKeyInternal() {
  DCHECK(iter_valid_ || resolved_intent_state_ == ResolvedIntentState::kValid)
      << "Expected iter_valid(" << iter_valid_ << ") || resolved_intent_state_("
      << resolved_intent_state_ << ") == ResolvedIntentState::kValid";
  // Choose latest subkey among regular and intent iterators.
  Slice subdockey_slice(
      !iter_valid_ ||
      (resolved_intent_state_ == ResolvedIntentState::kValid
          && iter_.key().compare(resolved_intent_sub_doc_key_encoded_) < 0)
      ? resolved_intent_key_prefix_.AsSlice() : iter_.key());
  // Seek to the first key for row containing found subdockey.
  auto dockey_size = DocKey::EncodedSize(subdockey_slice, DocKeyPart::WHOLE_DOC_KEY);
  if (!dockey_size.ok()) {
    status_ = dockey_size.status();
    return;
  }
  Seek(Slice(subdockey_slice.data(), *dockey_size));
}

void IntentAwareIterator::SeekIntentIterIfNeeded() {
  if (seek_intent_iter_needed_ == SeekIntentIterNeeded::kNoNeed || !status_.ok()) {
    return;
  }
  status_ = SetIntentUpperbound();
  if (!status_.ok()) {
    return;
  }
  switch (seek_intent_iter_needed_) {
    case SeekIntentIterNeeded::kNoNeed:
      break;
    case SeekIntentIterNeeded::kSeek:
      ROCKSDB_SEEK(&intent_iter_, seek_key_buffer_);
      SeekToSuitableIntent<Direction::kForward>();
      seek_intent_iter_needed_ = SeekIntentIterNeeded::kNoNeed;
      return;
    case SeekIntentIterNeeded::kSeekForward:
      SeekForwardToSuitableIntent(seek_key_buffer_);
      seek_intent_iter_needed_ = SeekIntentIterNeeded::kNoNeed;
      return;
  }
  FATAL_INVALID_ENUM_VALUE(SeekIntentIterNeeded, seek_intent_iter_needed_);
}

bool IntentAwareIterator::valid() {
  if (skip_future_records_needed_) {
    SkipFutureRecords(Direction::kForward);
  }
  SeekIntentIterIfNeeded();
  if (skip_future_intents_needed_) {
    SkipFutureIntents();
  }
  return !status_.ok() || iter_valid_ || resolved_intent_state_ == ResolvedIntentState::kValid;
}

bool IntentAwareIterator::IsEntryRegular() {
  if (PREDICT_FALSE(!iter_valid_)) {
    return false;
  }
  if (resolved_intent_state_ == ResolvedIntentState::kValid) {
    return iter_.key().compare(resolved_intent_sub_doc_key_encoded_) < 0;
  }
  return true;
}

Result<FetchKeyResult> IntentAwareIterator::FetchKey() {
  RETURN_NOT_OK(status_);
  FetchKeyResult result;
  if (IsEntryRegular()) {
    result.key = iter_.key();
    result.write_time = VERIFY_RESULT(DocHybridTime::DecodeFromEnd(&result.key));
    DCHECK(result.key.ends_with(ValueTypeAsChar::kHybridTime)) << result.key.ToDebugString();
    result.key.remove_suffix(1);
    result.same_transaction = false;
    max_seen_ht_.MakeAtLeast(result.write_time.hybrid_time());
  } else {
    DCHECK_EQ(ResolvedIntentState::kValid, resolved_intent_state_);
    result.key = resolved_intent_key_prefix_.AsSlice();
    result.write_time = GetIntentDocHybridTime();
    result.same_transaction = ResolvedIntentFromSameTransaction();
    max_seen_ht_.MakeAtLeast(resolved_intent_txn_dht_.hybrid_time());
  }
  VLOG(4) << "Fetched key " << SubDocKey::DebugSliceToString(result.key)
          << ", with time: " << result.write_time
          << ", while read bounds are: " << read_time_;
  return result;
}

Slice IntentAwareIterator::value() {
  if (IsEntryRegular()) {
    VLOG(4) << "IntentAwareIterator::value() returning iter_.value(): "
            << iter_.value().ToDebugHexString() << " or " << FormatSliceAsStr(iter_.value());
    return iter_.value();
  } else {
    DCHECK_EQ(ResolvedIntentState::kValid, resolved_intent_state_);
    VLOG(4) << "IntentAwareIterator::value() returning resolved_intent_value_: "
            << resolved_intent_value_.AsSlice().ToDebugHexString();
    return resolved_intent_value_;
  }
}

void IntentAwareIterator::SeekForwardRegular(const Slice& slice) {
  VLOG(4) << "SeekForwardRegular(" << SubDocKey::DebugSliceToString(slice) << ")";
  docdb::SeekForward(slice, &iter_);
  skip_future_records_needed_ = true;
}

bool IntentAwareIterator::SatisfyBounds(const Slice& slice) {
  return upperbound_.empty() || slice.compare(upperbound_) <= 0;
}

void IntentAwareIterator::ProcessIntent() {
  auto decode_result = DecodeStrongWriteIntent(
      txn_op_context_.get(), &intent_iter_, &transaction_status_cache_);
  if (!decode_result.ok()) {
    status_ = decode_result.status();
    return;
  }
  VLOG(4) << "Intent decode: " << DebugIntentKeyToString(intent_iter_.key())
          << " => " << intent_iter_.value().ToDebugHexString() << ", result: " << *decode_result;
  DOCDB_DEBUG_LOG(
      "resolved_intent_txn_dht_: $0 value_time: $1 read_time: $2",
      resolved_intent_txn_dht_.ToString(),
      decode_result->value_time.ToString(),
      read_time_.ToString());
  auto resolved_intent_time = decode_result->same_transaction ? intent_dht_from_same_txn_
                                                              : resolved_intent_txn_dht_;
  // If we already resolved intent that is newer that this one, we should ignore current
  // intent because we are interested in the most recent intent only.
  if (decode_result->value_time <= resolved_intent_time) {
    return;
  }

  // Ignore intent past read limit.
  auto max_allowed_time = decode_result->same_transaction
      ? read_time_.in_txn_limit : read_time_.global_limit;
  if (decode_result->value_time.hybrid_time() > max_allowed_time) {
    return;
  }

  if (resolved_intent_state_ == ResolvedIntentState::kNoIntent) {
    resolved_intent_key_prefix_.Reset(decode_result->intent_prefix);
    auto prefix = prefix_stack_.empty() ? Slice() : prefix_stack_.back();
    if (!decode_result->intent_prefix.starts_with(prefix)) {
      resolved_intent_state_ = ResolvedIntentState::kInvalidPrefix;
    } else if (!SatisfyBounds(decode_result->intent_prefix)) {
      resolved_intent_state_ = ResolvedIntentState::kNoIntent;
    } else {
      resolved_intent_state_ = ResolvedIntentState::kValid;
    }
  }
  if (decode_result->same_transaction) {
    intent_dht_from_same_txn_ = decode_result->value_time;
    // We set resolved_intent_txn_dht_ to maximum possible time (time higher than read_time_.read
    // will cause read restart or will be ignored if higher than read_time_.global_limit) in
    // order to ignore intents/values from other transactions. But we save origin intent time into
    // intent_dht_from_same_txn_, so we can compare time of intents for the same key from the same
    // transaction and select the latest one.
    resolved_intent_txn_dht_ = DocHybridTime(read_time_.read, kMaxWriteId);
  } else {
    resolved_intent_txn_dht_ = decode_result->value_time;
  }
  resolved_intent_value_.Reset(decode_result->intent_value);
}

void IntentAwareIterator::UpdateResolvedIntentSubDocKeyEncoded() {
  resolved_intent_sub_doc_key_encoded_.Reset(resolved_intent_key_prefix_.AsSlice());
  resolved_intent_sub_doc_key_encoded_.AppendValueType(ValueType::kHybridTime);
  resolved_intent_sub_doc_key_encoded_.AppendHybridTime(resolved_intent_txn_dht_);
  VLOG(4) << "Resolved intent SubDocKey: "
          << DebugDumpKeyToStr(resolved_intent_sub_doc_key_encoded_);
}

void IntentAwareIterator::SeekForwardToSuitableIntent(const KeyBytes &intent_key_prefix) {
  DOCDB_DEBUG_SCOPE_LOG(intent_key_prefix.ToString(),
                        std::bind(&IntentAwareIterator::DebugDump, this));
  if (resolved_intent_state_ != ResolvedIntentState::kNoIntent &&
      resolved_intent_key_prefix_.CompareTo(intent_key_prefix) >= 0) {
    return;
  }
  // Use ROCKSDB_SEEK() to force re-seek of "intent_iter_" in case the iterator was invalid by the
  // previous intent upperbound, but the upperbound has changed therefore requiring re-seek.
  ROCKSDB_SEEK(&intent_iter_, intent_key_prefix.AsSlice());
  SeekToSuitableIntent<Direction::kForward>();
}

template<Direction direction>
void IntentAwareIterator::SeekToSuitableIntent() {
  DOCDB_DEBUG_SCOPE_LOG("", std::bind(&IntentAwareIterator::DebugDump, this));
  resolved_intent_state_ = ResolvedIntentState::kNoIntent;
  resolved_intent_txn_dht_ = DocHybridTime::kMin;
  intent_dht_from_same_txn_ = DocHybridTime::kMin;
  auto prefix = prefix_stack_.empty() ? Slice() : prefix_stack_.back();

  // Find latest suitable intent for the first SubDocKey having suitable intents.
  while (intent_iter_.Valid()) {
    auto intent_key = intent_iter_.key();
    VLOG(4) << "Intent found: " << DebugIntentKeyToString(intent_key)
            << ", resolved state: " << yb::ToString(resolved_intent_state_);
    if (resolved_intent_state_ != ResolvedIntentState::kNoIntent &&
        // Only scan intents for the first SubDocKey having suitable intents.
        !IsIntentForTheSameKey(intent_key, resolved_intent_key_prefix_)) {
      break;
    }
    if (!intent_key.starts_with(prefix) || !SatisfyBounds(intent_key)) {
      break;
    }
    ProcessIntent();
    if (!status_.ok()) {
      return;
    }
    switch (direction) {
      case Direction::kForward:
        intent_iter_.Next();
        break;
      case Direction::kBackward:
        intent_iter_.Prev();
        break;
    }
  }
  if (resolved_intent_state_ != ResolvedIntentState::kNoIntent) {
    UpdateResolvedIntentSubDocKeyEncoded();
  }
}

void IntentAwareIterator::DebugDump() {
  bool is_valid = valid();
  LOG(INFO) << ">> IntentAwareIterator dump";
  LOG(INFO) << "iter_.Valid(): " << iter_.Valid();
  if (iter_.Valid()) {
    LOG(INFO) << "iter_.key(): " << DebugDumpKeyToStr(iter_.key());
  }
  if (intent_iter_.Initialized()) {
    LOG(INFO) << "intent_iter_.Valid(): " << intent_iter_.Valid();
    if (intent_iter_.Valid()) {
      LOG(INFO) << "intent_iter_.key(): " << intent_iter_.key().ToDebugHexString();
    }
  }
  LOG(INFO) << "resolved_intent_state_: " << yb::ToString(resolved_intent_state_);
  if (resolved_intent_state_ != ResolvedIntentState::kNoIntent) {
    LOG(INFO) << "resolved_intent_sub_doc_key_encoded_: "
              << DebugDumpKeyToStr(resolved_intent_sub_doc_key_encoded_);
  }
  LOG(INFO) << "valid(): " << is_valid;
  if (valid()) {
    auto key_data = FetchKey();
    if (key_data.ok()) {
      LOG(INFO) << "key(): " << DebugDumpKeyToStr(key_data->key)
                << ", doc_ht: " << key_data->write_time;
    } else {
      LOG(INFO) << "key(): fetch failed: " << key_data.status();
    }
  }
  LOG(INFO) << "<< IntentAwareIterator dump";
}

Status IntentAwareIterator::FindLatestIntentRecord(
    const Slice& key_without_ht,
    DocHybridTime* latest_record_ht,
    bool* found_later_intent_result) {
  const auto intent_prefix = GetIntentPrefixForKeyWithoutHt(key_without_ht);
  SeekForwardToSuitableIntent(intent_prefix);
  RETURN_NOT_OK(status_);
  if (resolved_intent_state_ != ResolvedIntentState::kValid) {
    return Status::OK();
  }

  auto time = GetIntentDocHybridTime();
  if (time > *latest_record_ht && resolved_intent_key_prefix_.CompareTo(intent_prefix) == 0) {
    *latest_record_ht = time;
    max_seen_ht_.MakeAtLeast(resolved_intent_txn_dht_.hybrid_time());
    *found_later_intent_result = true;
  }
  return Status::OK();
}

Status IntentAwareIterator::FindLatestRegularRecord(
    const Slice& key_without_ht,
    DocHybridTime* latest_record_ht,
    bool* found_later_regular_result) {
  DocHybridTime doc_ht;
  int other_encoded_ht_size = 0;
  RETURN_NOT_OK(CheckHybridTimeSizeAndValueType(iter_.key(), &other_encoded_ht_size));
  if (key_without_ht.size() + 1 + other_encoded_ht_size == iter_.key().size() &&
      iter_.key().starts_with(key_without_ht)) {
    RETURN_NOT_OK(DecodeHybridTimeFromEndOfKey(iter_.key(), &doc_ht));

    if (doc_ht > *latest_record_ht) {
      *latest_record_ht = doc_ht;
      max_seen_ht_.MakeAtLeast(doc_ht.hybrid_time());
      *found_later_regular_result = true;
    }
  }
  return Status::OK();
}

Status IntentAwareIterator::FindLatestRecord(
    const Slice& key_without_ht,
    DocHybridTime* latest_record_ht,
    Slice* result_value) {
  if (!latest_record_ht)
    return STATUS(Corruption, "latest_record_ht should not be a null pointer");
  DCHECK_ONLY_NOTNULL(latest_record_ht);
  VLOG(4) << "FindLatestRecord(" << SubDocKey::DebugSliceToString(key_without_ht) << ", "
          << *latest_record_ht << ")";
  DOCDB_DEBUG_SCOPE_LOG(
      SubDocKey::DebugSliceToString(key_without_ht) + ", " + yb::ToString(latest_record_ht) + ", "
      + yb::ToString(result_value),
      std::bind(&IntentAwareIterator::DebugDump, this));
  DCHECK(!DebugHasHybridTime(key_without_ht));

  RETURN_NOT_OK(status_);
  if (!valid()) {
    return Status::OK();
  }

  bool found_later_intent_result = false;
  if (intent_iter_.Initialized()) {
    RETURN_NOT_OK(FindLatestIntentRecord(
        key_without_ht, latest_record_ht, &found_later_intent_result));
  }

  seek_key_buffer_.Reserve(key_without_ht.size() + encoded_read_time_global_limit_.size() + 1);
  seek_key_buffer_.Reset(key_without_ht);
  AppendEncodedDocHt(encoded_read_time_global_limit_, &seek_key_buffer_);
  SeekForwardRegular(seek_key_buffer_);
  RETURN_NOT_OK(status_);
  // After SeekForwardRegular(), we need to call valid() to skip future records and see if the
  // current key still matches the pushed prefix if any. If it does not, we are done.
  if (!valid()) {
    return Status::OK();
  }

  bool found_later_regular_result = false;
  if (iter_valid_) {
    RETURN_NOT_OK(FindLatestRegularRecord(
        key_without_ht, latest_record_ht, &found_later_regular_result));
  }

  if (result_value) {
    if (found_later_regular_result) {
      *result_value = iter_.value();
    } else if (found_later_intent_result) {
      *result_value = resolved_intent_value_;
    }
  }
  return Status::OK();
}

void IntentAwareIterator::PushPrefix(const Slice& prefix) {
  VLOG(4) << "PushPrefix: " << SubDocKey::DebugSliceToString(prefix);
  prefix_stack_.push_back(prefix);
  skip_future_records_needed_ = true;
  skip_future_intents_needed_ = true;
}

void IntentAwareIterator::PopPrefix() {
  prefix_stack_.pop_back();
  skip_future_records_needed_ = true;
  skip_future_intents_needed_ = true;
  VLOG(4) << "PopPrefix: "
          << (prefix_stack_.empty() ? std::string()
              : SubDocKey::DebugSliceToString(prefix_stack_.back()));
}

void IntentAwareIterator::SkipFutureRecords(const Direction direction) {
  skip_future_records_needed_ = false;
  if (!status_.ok()) {
    return;
  }
  auto prefix = prefix_stack_.empty() ? Slice() : prefix_stack_.back();
  while (iter_.Valid()) {
    if (!iter_.key().starts_with(prefix)) {
      VLOG(4) << "Unmatched prefix: " << SubDocKey::DebugSliceToString(iter_.key())
              << ", prefix: " << SubDocKey::DebugSliceToString(prefix);
      iter_valid_ = false;
      return;
    }
    if (!SatisfyBounds(iter_.key())) {
      VLOG(4) << "Out of bounds: " << SubDocKey::DebugSliceToString(iter_.key())
              << ", upperbound: " << SubDocKey::DebugSliceToString(upperbound_);
      iter_valid_ = false;
      return;
    }
    Slice encoded_doc_ht = iter_.key();
    int doc_ht_size = 0;
    auto decode_status = DocHybridTime::CheckAndGetEncodedSize(encoded_doc_ht, &doc_ht_size);
    if (!decode_status.ok()) {
      LOG(ERROR) << "Decode doc ht from key failed: " << decode_status
                 << ", key: " << iter_.key().ToDebugHexString();
      status_ = std::move(decode_status);
      return;
    }
    encoded_doc_ht.remove_prefix(encoded_doc_ht.size() - doc_ht_size);
    auto value = iter_.value();
    auto value_type = DecodeValueType(value);
    if (value_type == ValueType::kHybridTime) {
      // Value came from a transaction, we could try to filter it by original intent time.
      Slice encoded_intent_doc_ht = value;
      encoded_intent_doc_ht.consume_byte();
      if (encoded_intent_doc_ht.compare(Slice(encoded_read_time_local_limit_)) > 0 &&
          encoded_doc_ht.compare(Slice(encoded_read_time_global_limit_)) > 0) {
        iter_valid_ = true;
        return;
      }
    } else if (encoded_doc_ht.compare(Slice(encoded_read_time_local_limit_)) > 0) {
      iter_valid_ = true;
      return;
    }
    VLOG(4) << "Skipping because of time: " << SubDocKey::DebugSliceToString(iter_.key())
            << ", read time: " << read_time_;
    switch (direction) {
      case Direction::kForward:
        iter_.Next(); // TODO(dtxn) use seek with the same key, but read limit as doc hybrid time.
        break;
      case Direction::kBackward:
        iter_.Prev();
        break;
      default:
        status_ = STATUS_FORMAT(Corruption, "Unexpected direction: $0", direction);
        LOG(ERROR) << status_;
        iter_valid_ = false;
        return;
    }
  }
  iter_valid_ = false;
}

void IntentAwareIterator::SkipFutureIntents() {
  skip_future_intents_needed_ = false;
  if (!intent_iter_.Initialized() || !status_.ok()) {
    return;
  }
  auto prefix = prefix_stack_.empty() ? Slice() : prefix_stack_.back();
  if (resolved_intent_state_ != ResolvedIntentState::kNoIntent) {
    VLOG(4) << "Checking resolved intent subdockey: "
            << resolved_intent_key_prefix_.AsSlice().ToDebugHexString()
            << ", against new prefix: " << prefix.ToDebugHexString();
    auto compare_result = resolved_intent_key_prefix_.AsSlice().compare_prefix(prefix);
    if (compare_result == 0) {
      if (!SatisfyBounds(resolved_intent_key_prefix_.AsSlice())) {
        resolved_intent_state_ = ResolvedIntentState::kNoIntent;
      } else {
        resolved_intent_state_ = ResolvedIntentState::kValid;
      }
      return;
    } else if (compare_result > 0) {
      resolved_intent_state_ = ResolvedIntentState::kInvalidPrefix;
      return;
    }
  }
  SeekToSuitableIntent<Direction::kForward>();
}

Status IntentAwareIterator::SetIntentUpperbound() {
  if (iter_.Valid()) {
    intent_upperbound_keybytes_.Clear();
    // Strip ValueType::kHybridTime + DocHybridTime at the end of SubDocKey in iter_ and append
    // to upperbound with 0xff.
    Slice subdoc_key = iter_.key();
    int doc_ht_size = 0;
    RETURN_NOT_OK(DocHybridTime::CheckAndGetEncodedSize(subdoc_key, &doc_ht_size));
    subdoc_key.remove_suffix(1 + doc_ht_size);
    intent_upperbound_keybytes_.AppendRawBytes(subdoc_key);
    intent_upperbound_keybytes_.AppendValueType(ValueType::kMaxByte);
    intent_upperbound_ = intent_upperbound_keybytes_.AsSlice();
  } else {
    // In case the current position of the regular iterator is invalid, set the exclusive
    // upperbound to the beginning of the transaction metadata and reverse index region.
    ResetIntentUpperbound();
  }
  VLOG(4) << "SetIntentUpperbound = " << intent_upperbound_.ToDebugString();
  return Status::OK();
}

void IntentAwareIterator::ResetIntentUpperbound() {
  intent_upperbound_keybytes_.Clear();
  intent_upperbound_keybytes_.AppendValueType(ValueType::kTransactionId);
  intent_upperbound_ = intent_upperbound_keybytes_.AsSlice();
  VLOG(4) << "ResetIntentUpperbound = " << intent_upperbound_.ToDebugString();
}

}  // namespace docdb
}  // namespace yb
