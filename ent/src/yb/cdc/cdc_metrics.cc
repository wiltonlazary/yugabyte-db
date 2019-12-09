// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#include "yb/cdc/cdc_metrics.h"

#include "yb/util/metrics.h"
#include "yb/util/trace.h"


// CDC Tablet metrics.
METRIC_DEFINE_histogram(cdc, rpc_payload_bytes_responded, "CDC Bytes Responded",
  yb::MetricUnit::kBytes,
  "Payload size of responses to CDC GetChanges requests (only when records are included)",
  60000000LU /* max int */, 2 /* digits */);
METRIC_DEFINE_counter(cdc, rpc_heartbeats_responded, "CDC Rpc Heartbeat Count",
  yb::MetricUnit::kRequests,
  "Number of responses to CDC GetChanges requests without a record payload.");
METRIC_DEFINE_gauge_int64(cdc, last_read_opid_term, "CDC Last Read OpId (Term)",
  yb::MetricUnit::kOperations,
  "ID of the Last Read Producer Operation from a CDC GetChanges request. Format = term.index");
METRIC_DEFINE_gauge_int64(cdc, last_read_opid_index, "CDC Last Read OpId (Index)",
  yb::MetricUnit::kOperations,
  "ID of the Last Read Producer Operation from a CDC GetChanges request. Format = term.index");
METRIC_DEFINE_gauge_uint64(cdc, last_read_hybridtime, "CDC Last Read HybridTime.",
  yb::MetricUnit::kMicroseconds,
  "HybridTime of the Last Read Operation from a CDC GetChanges request");
METRIC_DEFINE_gauge_uint64(cdc, last_read_physicaltime, "CDC Last Read Physical TIme.",
  yb::MetricUnit::kMicroseconds,
  "Physical Time of the Last Read Operation from a CDC GetChanges request");
METRIC_DEFINE_gauge_int64(cdc, last_readable_opid_index, "CDC Last Readable OpId (Index)",
  yb::MetricUnit::kOperations,
  "Index of the Last Producer Operation that a CDC GetChanges request COULD read.");

// CDC Server Metrics
METRIC_DEFINE_counter(server, cdc_rpc_proxy_count, "CDC Rpc Proxy Count", yb::MetricUnit::kRequests,
  "Number of CDC GetChanges requests that required proxy forwarding");

namespace yb {
namespace cdc {

#define MINIT(x) x(METRIC_##x.Instantiate(entity))
#define GINIT(x) x(METRIC_##x.Instantiate(entity, 0))
CDCTabletMetrics::CDCTabletMetrics(const scoped_refptr<MetricEntity>& entity,
    const std::string& key)
    : MINIT(rpc_payload_bytes_responded),
      MINIT(rpc_heartbeats_responded),
      GINIT(last_read_opid_term),
      GINIT(last_read_opid_index),
      GINIT(last_read_hybridtime),
      GINIT(last_read_physicaltime),
      GINIT(last_readable_opid_index),
      entity_(entity),
      key_(key) { }

CDCServerMetrics::CDCServerMetrics(const scoped_refptr<MetricEntity>& entity)
    : MINIT(cdc_rpc_proxy_count),
      entity_(entity) { }
#undef MINIT
#undef GINIT

} // namespace cdc
} // namespace yb
