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

#ifndef YB_DOCDB_DOC_PATH_H_
#define YB_DOCDB_DOC_PATH_H_

#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "yb/docdb/doc_key.h"
#include "yb/docdb/primitive_value.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/string_util.h"

namespace yb {
namespace docdb {

// Identifies a particular subdocument inside the logical representation of the document database.
// By "logical representation" we mean that we are not concerned with the exact keys used in the
// underlying key-value store. This is very similar to a SubDocKey without a hybrid time, and can
// probably be merged with it.

class DocPath {
 public:
  template<class... T>
  DocPath(const KeyBytes& encoded_doc_key, T... subkeys) {
    encoded_doc_key_ = encoded_doc_key;
    AppendPrimitiveValues(&subkeys_, subkeys...);
  }

  template<class... T>
  DocPath(const Slice& encoded_doc_key, T... subkeys)
      : encoded_doc_key_(encoded_doc_key) {
    AppendPrimitiveValues(&subkeys_, subkeys...);
  }

  DocPath(const KeyBytes& encoded_doc_key, const vector<PrimitiveValue>& subkeys)
      : encoded_doc_key_(encoded_doc_key),
        subkeys_(subkeys) {
  }

  const KeyBytes& encoded_doc_key() const { return encoded_doc_key_; }

  int num_subkeys() const { return subkeys_.size(); }

  const PrimitiveValue& subkey(int i) const {
    assert(0 <= i && i < num_subkeys());
    return subkeys_[i];
  }

  std::string ToString() const {
    return strings::Substitute("DocPath($0, $1)",
        BestEffortDocDBKeyToStr(encoded_doc_key_), rocksdb::VectorToString(subkeys_));
  }

  void AddSubKey(const PrimitiveValue& subkey) {
    subkeys_.emplace_back(subkey);
  }

  void AddSubKey(PrimitiveValue&& subkey) {
    subkeys_.emplace_back(std::move(subkey));
  }

  const PrimitiveValue& last_subkey() const {
    assert(!subkeys_.empty());
    return subkeys_.back();
  }

  // Note: the hash is supposed to be uint16_t, but protobuf only supports uint32.
  // So this function takes in uint32_t.
  // TODO (akashnil): Add uint16 data type in docdb.
  static DocPath DocPathFromRedisKey(uint16_t hash, const string& key, const string& subkey = "") {
    DocPath doc_path = DocPath(DocKey::FromRedisKey(hash, key).Encode());
    if (!subkey.empty()) {
      doc_path.AddSubKey(PrimitiveValue(subkey));
    }
    return doc_path;
  }

  const std::vector<PrimitiveValue>& subkeys() const {
    return subkeys_;
  }

 private:
  // Encoded key identifying the document. This key can itself contain multiple components
  // (hash bucket, hashed components, range components).
  // TODO(mikhail): should this really be encoded?
  KeyBytes encoded_doc_key_;

  std::vector<PrimitiveValue> subkeys_;
};

inline std::ostream& operator << (std::ostream& out, const DocPath& doc_path) {
  return out << doc_path.ToString();
}

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_DOC_PATH_H_
