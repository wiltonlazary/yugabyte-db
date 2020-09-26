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

#include "yb/docdb/doc_write_batch.h"

#include "yb/docdb/doc_key.h"
#include "yb/rocksdb/db.h"
#include "yb/rocksdb/write_batch.h"
#include "yb/rocksutil/write_batch_formatter.h"

#include "yb/server/hybrid_clock.h"

#include "yb/docdb/doc_ttl_util.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/value_type.h"
#include "yb/docdb/kv_debug.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/enums.h"

using yb::BinaryOutputFormat;

using yb::server::HybridClock;

namespace yb {
namespace docdb {

DocWriteBatch::DocWriteBatch(const DocDB& doc_db,
                             InitMarkerBehavior init_marker_behavior,
                             std::atomic<int64_t>* monotonic_counter)
    : doc_db_(doc_db),
      init_marker_behavior_(init_marker_behavior),
      monotonic_counter_(monotonic_counter) {}

Status DocWriteBatch::SeekToKeyPrefix(LazyIterator* iter, bool has_ancestor) {
  subdoc_exists_ = false;
  current_entry_.value_type = ValueType::kInvalid;

  // Check the cache first.
  boost::optional<DocWriteBatchCache::Entry> cached_entry =
    cache_.Get(key_prefix_);
  if (cached_entry) {
    current_entry_ = *cached_entry;
    subdoc_exists_ = current_entry_.value_type != ValueType::kTombstone;
    return Status::OK();
  }
  return SeekToKeyPrefix(iter->Iterator(), has_ancestor);
}

Status DocWriteBatch::SeekToKeyPrefix(IntentAwareIterator* doc_iter, bool has_ancestor) {
  const auto prev_subdoc_ht = current_entry_.doc_hybrid_time;
  const auto prev_key_prefix_exact = current_entry_.found_exact_key_prefix;

  // Seek the value.
  doc_iter->Seek(key_prefix_.AsSlice());
  if (!doc_iter->valid()) {
    return Status::OK();
  }

  auto key_data = VERIFY_RESULT(doc_iter->FetchKey());
  if (!key_prefix_.IsPrefixOf(key_data.key)) {
    return Status::OK();
  }

  // Checking for expiration.
  uint64_t merge_flags = 0;
  MonoDelta ttl;
  Slice recent_value = doc_iter->value();
  RETURN_NOT_OK(Value::DecodePrimitiveValueType(
      recent_value, &(current_entry_.value_type),
      &merge_flags, &ttl, &(current_entry_.user_timestamp)));

  bool has_expired;
  CHECK_OK(HasExpiredTTL(key_data.write_time.hybrid_time(), ttl,
                         doc_iter->read_time().read, &has_expired));

  if (has_expired) {
    current_entry_.value_type = ValueType::kTombstone;
    current_entry_.doc_hybrid_time = key_data.write_time;
    cache_.Put(key_prefix_, current_entry_);
    return Status::OK();
  }

  Slice value;
  RETURN_NOT_OK(doc_iter->NextFullValue(&key_data.write_time, &value, &key_data.key));

  if (!doc_iter->valid()) {
    return Status::OK();
  }

  // If the first key >= key_prefix_ in RocksDB starts with key_prefix_, then a
  // document/subdocument pointed to by key_prefix_ exists, or has been recently deleted.
  if (key_prefix_.IsPrefixOf(key_data.key)) {
    // No need to decode again if no merge records were encountered.
    if (value != recent_value)
      RETURN_NOT_OK(Value::DecodePrimitiveValueType(value, &(current_entry_.value_type),
          /* merge flags */ nullptr, /* ttl */ nullptr, &(current_entry_.user_timestamp)));
    current_entry_.found_exact_key_prefix = key_prefix_ == key_data.key;
    current_entry_.doc_hybrid_time = key_data.write_time;

    // TODO: with optional init markers we can find something that is more than one level
    //       deep relative to the current prefix.
    // Note: this comment was originally placed right before the line decoding the HybridTime,
    // which has since been refactored away. Not sure what this means, so keeping it for now.

    // Cache the results of reading from RocksDB so that we don't have to read again in a later
    // operation in the same DocWriteBatch.
    DOCDB_DEBUG_LOG("Writing to DocWriteBatchCache: $0",
                    BestEffortDocDBKeyToStr(key_prefix_));

    if (has_ancestor && prev_subdoc_ht > current_entry_.doc_hybrid_time &&
        prev_key_prefix_exact) {
      // We already saw an object init marker or a tombstone one level higher with a higher
      // hybrid_time, so just ignore this key/value pair. This had to be added when we switched
      // from a format with intermediate hybrid_times to our current format without them.
      //
      // Example (from a real test case):
      //
      // SubDocKey(DocKey([], ["a"]), [HT(38)]) -> {}
      // SubDocKey(DocKey([], ["a"]), [HT(37)]) -> DEL
      // SubDocKey(DocKey([], ["a"]), [HT(36)]) -> false
      // SubDocKey(DocKey([], ["a"]), [HT(1)]) -> {}
      // SubDocKey(DocKey([], ["a"]), ["y", HT(35)]) -> "lD\x97\xaf^m\x0a1\xa0\xfc\xc8YM"
      //
      // Caveat (04/17/2017): the HybridTime encoding in the above example is outdated.
      //
      // In the above layout, if we try to set "a.y.x" to a new value, we first seek to the
      // document key "a" and find that it exists, but then we seek to "a.y" and find that it
      // also exists as a primitive value (assuming we don't check the hybrid_time), and
      // therefore we can't create "a.y.x", which would be incorrect.
      subdoc_exists_ = false;
    } else {
      cache_.Put(key_prefix_, current_entry_);
      subdoc_exists_ = current_entry_.value_type != ValueType::kTombstone;
    }
  }
  return Status::OK();
}

Result<bool> DocWriteBatch::SetPrimitiveInternalHandleUserTimestamp(
    const Value &value,
    LazyIterator* iter) {
  bool should_apply = true;
  if (value.user_timestamp() != Value::kInvalidUserTimestamp) {
    // Seek for the older version of the key that we're about to write to. This is essentially a
    // NOOP if we've already performed the seek due to the cache.
    RETURN_NOT_OK(SeekToKeyPrefix(iter));
    // We'd like to include tombstones in our timestamp comparisons as well.
    if ((subdoc_exists_ || current_entry_.value_type == ValueType::kTombstone) &&
        current_entry_.found_exact_key_prefix) {
      if (current_entry_.user_timestamp != Value::kInvalidUserTimestamp) {
        should_apply = value.user_timestamp() >= current_entry_.user_timestamp;
      } else {
        // Look at the hybrid time instead.
        const DocHybridTime& doc_hybrid_time = current_entry_.doc_hybrid_time;
        if (doc_hybrid_time.hybrid_time().is_valid()) {
          should_apply = value.user_timestamp() >=
              doc_hybrid_time.hybrid_time().GetPhysicalValueMicros();
        }
      }
    }
  }
  return should_apply;
}

CHECKED_STATUS DocWriteBatch::SetPrimitiveInternal(
    const DocPath& doc_path,
    const Value& value,
    LazyIterator* iter,
    const bool is_deletion,
    const int num_subkeys) {
  // The write_id is always incremented by one for each new element of the write batch.
  if (put_batch_.size() > numeric_limits<IntraTxnWriteId>::max()) {
    return STATUS_SUBSTITUTE(
        NotSupported,
        "Trying to add more than $0 key/value pairs in the same single-shard txn.",
        numeric_limits<IntraTxnWriteId>::max());
  }

  if (value.has_user_timestamp() && !optional_init_markers()) {
    return STATUS(IllegalState,
                  "User Timestamp is only supported for Optional Init Markers");
  }

  // We need the write_id component of DocHybridTime to disambiguate between writes in the same
  // WriteBatch, as they will have the same HybridTime when committed. E.g. if we insert, delete,
  // and re-insert the same column in one WriteBatch, we need to know the order of these operations.
  const auto write_id = static_cast<IntraTxnWriteId>(put_batch_.size());
  const DocHybridTime hybrid_time = DocHybridTime(HybridTime::kMax, write_id);

  for (int subkey_index = 0; subkey_index < num_subkeys; ++subkey_index) {
    const PrimitiveValue& subkey = doc_path.subkey(subkey_index);

    // We don't need to check if intermediate documents already exist if init markers are optional,
    // or if we already know they exist (either from previous reads or our own writes in the same
    // single-shard operation.)

    if (optional_init_markers() || subdoc_exists_) {
      if (required_init_markers() && !IsObjectType(current_entry_.value_type)) {
        // REDIS
        // ~~~~~
        // We raise this error only if init markers are mandatory.
        return STATUS_FORMAT(IllegalState,
                             "Cannot set values inside a subdocument of type $0",
                             current_entry_.value_type);
      }
      if (optional_init_markers()) {
        // CASSANDRA
        // ~~~~~~~~~
        // In the case where init markers are optional, we don't need to check existence of
        // the current subdocument. Although if we have a user timestamp specified, we need to
        // check whether the provided user timestamp is higher than what is already present. If
        // an intermediate subdocument is found with a higher timestamp, we consider it as an
        // overwrite and skip the entire write.
        auto should_apply = SetPrimitiveInternalHandleUserTimestamp(value, iter);
        RETURN_NOT_OK(should_apply);
        if (!should_apply.get()) {
          return Status::OK();
        }
        subkey.AppendToKey(&key_prefix_);
      } else if (subkey_index == num_subkeys - 1 && !is_deletion) {
        // REDIS
        // ~~~~~
        // We don't need to perform a RocksDB read at the last level for upserts, we just overwrite
        // the value within the last subdocument with what we're trying to write. We still perform
        // the read for deletions, because we try to avoid writing a new tombstone if the data is
        // not there anyway.
        if (!subdoc_exists_) {
          return STATUS(IllegalState, "Subdocument is supposed to exist.");
        }
        if (!IsObjectType(current_entry_.value_type)) {
          return STATUS(IllegalState, "Expected object subdocument type.");
        }
        subkey.AppendToKey(&key_prefix_);
      } else {
        // REDIS
        // ~~~~~
        // We need to check if the subdocument at this subkey exists.
        if (!subdoc_exists_) {
          return STATUS(IllegalState, "Subdocument is supposed to exist. $0");
        }
        if (!IsObjectType(current_entry_.value_type)) {
          return STATUS(IllegalState, "Expected object subdocument type. $0");
        }
        subkey.AppendToKey(&key_prefix_);
        RETURN_NOT_OK(SeekToKeyPrefix(iter, true));
        if (is_deletion && !subdoc_exists_) {
          // A parent subdocument of the value we're trying to delete, or that value itself, does
          // not exist, nothing to do.
          //
          // TODO: in Redis's HDEL command we need to count the number of fields deleted, so we need
          // to count the deletes that are actually happening.
          // See http://redis.io/commands/hdel
          DOCDB_DEBUG_LOG("Subdocument does not exist at subkey level $0 (subkey: $1)",
                          subkey_index, subkey.ToString());
          return Status::OK();
        }
      }
    } else {
      // REDIS
      // ~~~~~
      // The subdocument at the current level does not exist.
      if (is_deletion) {
        // A parent subdocument of the subdocument we're trying to delete does not exist, nothing
        // to do.
        return Status::OK();
      }

      DCHECK(!value.has_user_timestamp());

      // Add the parent key to key/value batch before appending the encoded HybridTime to it.
      // (We replicate key/value pairs without the HybridTime and only add it before writing to
      // RocksDB.)
      put_batch_.emplace_back(key_prefix_.ToStringBuffer(), string(1, ValueTypeAsChar::kObject));

      // Update our local cache to record the fact that we're adding this subdocument, so that
      // future operations in this DocWriteBatch don't have to add it or look for it in RocksDB.
      cache_.Put(key_prefix_, hybrid_time, ValueType::kObject);
      subkey.AppendToKey(&key_prefix_);
    }
  }

  // We need to handle the user timestamp if present.
  auto should_apply = SetPrimitiveInternalHandleUserTimestamp(value, iter);
  RETURN_NOT_OK(should_apply);
  if (should_apply.get()) {
    // The key in the key/value batch does not have an encoded HybridTime.
    put_batch_.emplace_back(key_prefix_.ToStringBuffer(), value.Encode());

    // The key we use in the DocWriteBatchCache does not have a final hybrid_time, because that's
    // the key we expect to look up.
    cache_.Put(key_prefix_, hybrid_time, value.primitive_value().value_type(),
               value.user_timestamp());
  }

  return Status::OK();
}

Status DocWriteBatch::SetPrimitive(
    const DocPath& doc_path,
    const Value& value,
    LazyIterator* iter) {
  DOCDB_DEBUG_LOG("Called SetPrimitive with doc_path=$0, value=$1",
                  doc_path.ToString(), value.ToString());
  current_entry_.doc_hybrid_time = DocHybridTime::kMin;
  const int num_subkeys = doc_path.num_subkeys();
  const bool is_deletion = value.primitive_value().value_type() == ValueType::kTombstone;

  key_prefix_ = doc_path.encoded_doc_key();

  // If we are overwriting an entire document with a primitive value (not deleting it), we don't
  // need to perform any reads from RocksDB at all.
  //
  // Even if we are deleting a document, but we don't need to get any feedback on whether the
  // deletion was performed or the document was not there to begin with, we could also skip the
  // read as an optimization.
  if (num_subkeys > 0 || is_deletion) {
    if (required_init_markers()) {
      // Navigate to the root of the document. We don't yet know whether the document exists or when
      // it was last updated.
      RETURN_NOT_OK(SeekToKeyPrefix(iter, false));
      DOCDB_DEBUG_LOG("Top-level document exists: $0", subdoc_exists_);
      if (!subdoc_exists_ && is_deletion) {
        DOCDB_DEBUG_LOG("We're performing a deletion, and the document is not present. "
                        "Nothing to do.");
        return Status::OK();
      }
    }
  }
  return SetPrimitiveInternal(doc_path, value, iter, is_deletion, num_subkeys);
}

Status DocWriteBatch::SetPrimitive(const DocPath& doc_path,
                                   const Value& value,
                                   const ReadHybridTime& read_ht,
                                   CoarseTimePoint deadline,
                                   rocksdb::QueryId query_id) {
  DOCDB_DEBUG_LOG("Called with doc_path=$0, value=$1",
                  doc_path.ToString(), value.ToString());

  std::function<std::unique_ptr<IntentAwareIterator>()> createrator =
    [doc_path, query_id, deadline, read_ht, this]() {
      return yb::docdb::CreateIntentAwareIterator(
          doc_db_,
          BloomFilterMode::USE_BLOOM_FILTER,
          doc_path.encoded_doc_key().AsSlice(),
          query_id,
          /*txn_op_context*/ boost::none,
          deadline,
          read_ht);
    };

  LazyIterator iter(&createrator);

  return SetPrimitive(doc_path, value, &iter);
}

Status DocWriteBatch::ExtendSubDocument(
    const DocPath& doc_path,
    const SubDocument& value,
    const ReadHybridTime& read_ht,
    const CoarseTimePoint deadline,
    rocksdb::QueryId query_id,
    MonoDelta ttl,
    UserTimeMicros user_timestamp) {
  if (IsObjectType(value.value_type())) {
    const auto& map = value.object_container();
    for (const auto& ent : map) {
      DocPath child_doc_path = doc_path;
      if (ent.first.value_type() != ValueType::kArray)
          child_doc_path.AddSubKey(ent.first);
      RETURN_NOT_OK(ExtendSubDocument(child_doc_path, ent.second,
                                      read_ht, deadline, query_id, ttl, user_timestamp));
    }
  } else if (value.value_type() == ValueType::kArray) {
    RETURN_NOT_OK(ExtendList(
        doc_path, value, read_ht, deadline, query_id, ttl, user_timestamp));
  } else {
    if (!value.IsTombstoneOrPrimitive()) {
      return STATUS_FORMAT(
          InvalidArgument,
          "Found unexpected value type $0. Expecting a PrimitiveType or a Tombstone",
          value.value_type());
    }
    RETURN_NOT_OK(SetPrimitive(doc_path, Value(value, ttl, user_timestamp),
                               read_ht, deadline, query_id));
  }
  return Status::OK();
}

Status DocWriteBatch::InsertSubDocument(
    const DocPath& doc_path,
    const SubDocument& value,
    const ReadHybridTime& read_ht,
    const CoarseTimePoint deadline,
    rocksdb::QueryId query_id,
    MonoDelta ttl,
    UserTimeMicros user_timestamp,
    bool init_marker_ttl) {
  if (!value.IsTombstoneOrPrimitive()) {
    auto key_ttl = init_marker_ttl ? ttl : Value::kMaxTtl;
    RETURN_NOT_OK(SetPrimitive(
        doc_path, Value(PrimitiveValue(value.value_type()), key_ttl, user_timestamp),
        read_ht, deadline, query_id));
  }
  return ExtendSubDocument(doc_path, value, read_ht, deadline, query_id, ttl, user_timestamp);
}

Status DocWriteBatch::ExtendList(
    const DocPath& doc_path,
    const SubDocument& value,
    const ReadHybridTime& read_ht,
    const CoarseTimePoint deadline,
    rocksdb::QueryId query_id,
    MonoDelta ttl,
    UserTimeMicros user_timestamp) {
  if (monotonic_counter_ == nullptr) {
    return STATUS(IllegalState, "List cannot be extended if monotonic_counter_ is uninitialized");
  }
  if (value.value_type() != ValueType::kArray) {
    return STATUS_FORMAT(
        InvalidArgument,
        "Expecting Subdocument of type kArray, found $0",
        value.value_type());
  }
  const std::vector<SubDocument>& list = value.array_container();
  // It is assumed that there is an exclusive lock on the list key.
  // The lock ensures that there isn't another thread picking ArrayIndexes for the same list.
  // No additional lock is required.
  int64_t index =
      std::atomic_fetch_add(monotonic_counter_, static_cast<int64_t>(list.size()));
  // PREPEND - adding in reverse order with negated index
  if (value.GetExtendOrder() == ListExtendOrder::PREPEND_BLOCK) {
    for (size_t i = list.size(); i > 0; i--) {
      DocPath child_doc_path = doc_path;
      index++;
      child_doc_path.AddSubKey(PrimitiveValue::ArrayIndex(-index));
      RETURN_NOT_OK(ExtendSubDocument(child_doc_path, list[i - 1],
                                      read_ht, deadline, query_id, ttl, user_timestamp));
    }
  } else {
    for (size_t i = 0; i < list.size(); i++) {
      DocPath child_doc_path = doc_path;
      index++;
      child_doc_path.AddSubKey(PrimitiveValue::ArrayIndex(
          value.GetExtendOrder() == ListExtendOrder::APPEND ? index : -index));
      RETURN_NOT_OK(ExtendSubDocument(child_doc_path, list[i],
                                      read_ht, deadline, query_id, ttl, user_timestamp));
    }
  }
  return Status::OK();
}

Status DocWriteBatch::ReplaceInList(
    const DocPath &doc_path,
    const std::vector<int>& indices,
    const std::vector<SubDocument>& values,
    const ReadHybridTime& read_ht,
    const CoarseTimePoint deadline,
    const rocksdb::QueryId query_id,
    const Direction dir,
    const int64_t start_index,
    std::vector<string>* results,
    MonoDelta default_ttl,
    MonoDelta write_ttl,
    bool is_cql) {
  SubDocKey sub_doc_key;
  RETURN_NOT_OK(sub_doc_key.FromDocPath(doc_path));
  key_prefix_ = sub_doc_key.Encode();

  auto iter = yb::docdb::CreateIntentAwareIterator(
      doc_db_,
      BloomFilterMode::USE_BLOOM_FILTER,
      key_prefix_.AsSlice(),
      query_id,
      /*txn_op_context*/ boost::none,
      deadline,
      read_ht);

  Slice value_slice;
  SubDocKey found_key;
  int current_index = start_index;
  int replace_index = 0;

  if (dir == Direction::kForward) {
    // Ensure we seek directly to indices and skip init marker if it exists.
    key_prefix_.AppendValueType(ValueType::kArrayIndex);
    RETURN_NOT_OK(SeekToKeyPrefix(iter.get(), false));
  } else {
    // We would like to seek past the entire list and go backwards.
    key_prefix_.AppendValueType(ValueType::kMaxByte);
    iter->PrevSubDocKey(key_prefix_);
    key_prefix_.RemoveValueTypeSuffix(ValueType::kMaxByte);
    key_prefix_.AppendValueType(ValueType::kArrayIndex);
  }

  FetchKeyResult key_data;
  while (true) {
    if (indices[replace_index] <= 0 || !iter->valid() ||
        !(key_data = VERIFY_RESULT(iter->FetchKey())).key.starts_with(key_prefix_)) {
      return is_cql ?
        STATUS_SUBSTITUTE(
          QLError,
          "Unable to replace items into list, expecting index $0, reached end of list with size $1",
          indices[replace_index] - 1, // YQL layer list index starts from 0, not 1 as in DocDB.
          current_index) :
        STATUS_SUBSTITUTE(Corruption,
          "Index Error: $0, reached beginning of list with size $1",
          indices[replace_index] - 1, // YQL layer list index starts from 0, not 1 as in DocDB.
          current_index);
    }

    RETURN_NOT_OK(found_key.FullyDecodeFrom(key_data.key, HybridTimeRequired::kFalse));

    MonoDelta entry_ttl;
    ValueType value_type;
    value_slice = iter->value();
    RETURN_NOT_OK(Value::DecodePrimitiveValueType(value_slice, &value_type, nullptr, &entry_ttl));

    bool has_expired = value_type == ValueType::kTombstone;
    // Redis lists do not have element-level TTL.
    if (!has_expired && is_cql) {
      entry_ttl = ComputeTTL(entry_ttl, default_ttl);
      RETURN_NOT_OK(HasExpiredTTL(
          key_data.write_time.hybrid_time(), entry_ttl, read_ht.read, &has_expired));
    }

    if (has_expired) {
      found_key.KeepPrefix(sub_doc_key.num_subkeys()+1);
      if (dir == Direction::kForward) {
        iter->SeekPastSubKey(key_data.key);
      } else {
        iter->PrevSubDocKey(KeyBytes(key_data.key));
      }
      continue;
    }

    // TODO (rahul): it may be cleaner to put this in the read path.
    // The code below is meant specifically for POP functionality in Redis lists.
    if (results) {
      Value v;
      RETURN_NOT_OK(v.Decode(iter->value()));
      results->push_back(v.primitive_value().GetString());
    }

    if (dir == Direction::kForward)
      current_index++;
    else
      current_index--;

    // Should we verify that the subkeys are indeed numbers as list indices should be?
    // Or just go in order for the index'th largest key in any subdocument?
    if (current_index == indices[replace_index]) {
      // When inserting, key_prefix_ is modified.
      KeyBytes array_index_prefix(key_prefix_);
      DocPath child_doc_path = doc_path;
      child_doc_path.AddSubKey(found_key.subkeys()[sub_doc_key.num_subkeys()]);
      RETURN_NOT_OK(InsertSubDocument(child_doc_path, values[replace_index],
                                      read_ht, deadline, query_id, write_ttl));
      replace_index++;
      if (replace_index == indices.size()) {
        return Status::OK();
      }
      key_prefix_ = array_index_prefix;
    }

    if (dir == Direction::kForward) {
      iter->SeekPastSubKey(key_data.key);
    } else {
      iter->PrevSubDocKey(KeyBytes(key_data.key));
    }
  }
}

void DocWriteBatch::Clear() {
  put_batch_.clear();
  cache_.Clear();
}

void DocWriteBatch::MoveToWriteBatchPB(KeyValueWriteBatchPB *kv_pb) {
  kv_pb->mutable_write_pairs()->Reserve(put_batch_.size());
  for (auto& entry : put_batch_) {
    KeyValuePairPB* kv_pair = kv_pb->add_write_pairs();
    kv_pair->mutable_key()->swap(entry.first);
    kv_pair->mutable_value()->swap(entry.second);
  }
}

void DocWriteBatch::TEST_CopyToWriteBatchPB(KeyValueWriteBatchPB *kv_pb) const {
  kv_pb->mutable_write_pairs()->Reserve(put_batch_.size());
  for (auto& entry : put_batch_) {
    KeyValuePairPB* kv_pair = kv_pb->add_write_pairs();
    kv_pair->mutable_key()->assign(entry.first);
    kv_pair->mutable_value()->assign(entry.second);
  }
}

// ------------------------------------------------------------------------------------------------
// Converting a RocksDB write batch to a string.
// ------------------------------------------------------------------------------------------------

class DocWriteBatchFormatter : public WriteBatchFormatter {
 public:
  DocWriteBatchFormatter(
      StorageDbType storage_db_type,
      BinaryOutputFormat binary_output_format)
      : WriteBatchFormatter(binary_output_format),
        storage_db_type_(storage_db_type) {}
 protected:
  std::string FormatKey(const Slice& key) override {
    const auto key_result = DocDBKeyToDebugStr(key, storage_db_type_);
    if (key_result.ok()) {
      return *key_result;
    }
    return Format(
        "$0 (error: $1)",
        WriteBatchFormatter::FormatKey(key),
        key_result.status());
  }

 private:
  StorageDbType storage_db_type_;
};

Result<std::string> WriteBatchToString(
    const rocksdb::WriteBatch& write_batch,
    StorageDbType storage_db_type,
    BinaryOutputFormat binary_output_format) {
  DocWriteBatchFormatter formatter(storage_db_type, binary_output_format);
  RETURN_NOT_OK(write_batch.Iterate(&formatter));
  return formatter.str();
}

}  // namespace docdb
}  // namespace yb
