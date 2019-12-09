#!/usr/bin/env bash
#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
##
# Script to find and run protoc to generate protocol buf files.
# Should be used exclusively by Maven.
#

# The following only applies to changes made to this file as part of YugaByte development.
#
# Portions Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#

. "${BASH_SOURCE%/*}/../../../build-support/common-build-env.sh"

# In case we are on NFS, try to use the shared thirdparty, if possible.
find_thirdparty_dir

if is_mac; then
  THIRDPARTY_BUILD_TYPE=clang_uninstrumented
else
  THIRDPARTY_BUILD_TYPE=uninstrumented
fi

PROTOC_BIN="$YB_THIRDPARTY_DIR/installed/$THIRDPARTY_BUILD_TYPE/bin/protoc"
if [[ ! -f $PROTOC_BIN ]]; then
  if which protoc > /dev/null; then
    PROTOC_BIN=$( which protoc )
  else
    fatal "Error: protoc is missing at '$PROTOC_BIN' (YB_THIRDPARTY_DIR=$YB_THIRDPARTY_DIR) and " \
          "on the PATH"
  fi
fi

set +e
"$PROTOC_BIN" "$@"
exit_code=$?
set -e

if [[ $exit_code -ne 0 ]]; then
  log "protoc command failed with exit code $exit_code: ( cd \"$PWD\" && \"$PROTOC_BIN\" $* )"
fi
exit "$exit_code"
