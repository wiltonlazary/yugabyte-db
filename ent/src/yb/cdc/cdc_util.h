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

#ifndef ENT_SRC_YB_CDC_CDC_UTIL_H
#define ENT_SRC_YB_CDC_CDC_UTIL_H

#include <stdlib.h>
#include <string>
#include <boost/functional/hash.hpp>

#include "yb/util/format.h"

namespace yb {
namespace cdc {

struct ConsumerTabletInfo {
  std::string tablet_id;
  std::string table_id;
};

struct ProducerTabletInfo {
  std::string universe_uuid; /* needed on Consumer side for uniqueness. Empty on Producer */
  std::string stream_id; /* unique ID on Producer, but not on Consumer. */
  std::string tablet_id;

  bool operator==(const ProducerTabletInfo& other) const {
    return universe_uuid == other.universe_uuid &&
           stream_id == other.stream_id &&
           tablet_id == other.tablet_id;
  }

  std::string ToString() const {
    return Format("{ universe_uuid: $0 stream_id: $1 tablet_id: $2 }",
                  universe_uuid, stream_id, tablet_id);
  }

  struct Hash {
    std::size_t operator()(const ProducerTabletInfo& p) const noexcept {
      std::size_t hash = 0;
      boost::hash_combine(hash, p.universe_uuid);
      boost::hash_combine(hash, p.stream_id);
      boost::hash_combine(hash, p.tablet_id);

      return hash;
    }
  };
};

inline size_t hash_value(const ProducerTabletInfo& p) noexcept {
  return ProducerTabletInfo::Hash()(p);
}

} // namespace cdc
} // namespace yb


#endif // ENT_SRC_YB_CDC_CDC_UTIL_H
