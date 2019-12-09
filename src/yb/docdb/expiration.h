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

#ifndef YB_DOCDB_EXPIRATION_H
#define YB_DOCDB_EXPIRATION_H

#include "yb/server/hybrid_clock.h"

#include "yb/docdb/value.h"

namespace yb {
namespace docdb {

// Useful for calculating expiration.
struct Expiration {
  Expiration() :
    ttl(Value::kMaxTtl) {}

  explicit Expiration(MonoDelta default_ttl) :
    ttl(default_ttl) {}

  explicit Expiration(HybridTime new_write_ht) :
    ttl(Value::kMaxTtl),
    write_ht(new_write_ht) {}

  explicit Expiration(HybridTime new_write_ht, MonoDelta new_ttl) :
    ttl(new_ttl),
    write_ht(new_write_ht) {}

  MonoDelta ttl;
  HybridTime write_ht = HybridTime::kMin;

  // A boolean which dictates whether the TTL of kMaxValue
  // should override the existing TTL. Not compatible with
  // the concept of default TTL when set to true.
  bool always_override = false;

  Result<MonoDelta> ComputeRelativeTtl(const HybridTime& input_time) {
    if (input_time < write_ht)
      return STATUS(Corruption, "Read time earlier than record write time.");
    if (ttl == Value::kMaxTtl || ttl.IsNegative())
      return ttl;
    MonoDelta elapsed_time = MonoDelta::FromNanoseconds(
        server::HybridClock::GetPhysicalValueNanos(input_time) -
        server::HybridClock::GetPhysicalValueNanos(write_ht));
    // This way, we keep the default TTL, and all negative TTLs are expired.
    MonoDelta new_ttl(ttl);
    return new_ttl -= elapsed_time;
  }

  std::string ToString() const {
    return Format("{ ttl: $0 write_ht: $1 always_override: $2 }", ttl, write_ht, always_override);
  }
};

}  // namespace docdb
}  // namespace yb

#endif // YB_DOCDB_EXPIRATION_H
