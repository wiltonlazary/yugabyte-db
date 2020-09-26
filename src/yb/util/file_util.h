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

#ifndef YB_UTIL_FILE_UTIL_H
#define YB_UTIL_FILE_UTIL_H

#include <string>

#include "yb/rocksdb/env.h"

#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/util/path_util.h"
#include "yb/util/status.h"

namespace yb {

YB_STRONGLY_TYPED_BOOL(CreateIfMissing);
YB_STRONGLY_TYPED_BOOL(UseHardLinks);

// TODO(unify_env): Temporary workaround until Env/Files from rocksdb and yb are unified
// (https://github.com/yugabyte/yugabyte-db/issues/1661).

// Following two functions returns OK if the file at `path` exists.
// NotFound if the named file does not exist, the calling process does not have permission to
//          determine whether this file exists, or if the path is invalid.
// IOError if an IO Error was encountered.
// Uses specified `env` environment implementation to do the actual file existence checking.
inline CHECKED_STATUS FileExists(Env* env, const std::string& path) {
  return env->FileExists(path) ? Status::OK() : STATUS(NotFound, "");
}

inline CHECKED_STATUS FileExists(rocksdb::Env* env, const std::string& path) {
  return env->FileExists(path);
}

using yb::env_util::CopyFile;

// Copies directory from `src_dir` to `dest_dir` using `env`.
// use_hard_links specifies whether to create hard links instead of actual file copying.
// create_if_missing specifies whether to create dest dir if doesn't exist or return an error.
// Returns error status in case of I/O errors.
template <class TEnv>
CHECKED_STATUS CopyDirectory(
    TEnv* env, const string& src_dir, const string& dest_dir, UseHardLinks use_hard_links,
    CreateIfMissing create_if_missing) {
  RETURN_NOT_OK_PREPEND(
      FileExists(env, src_dir), Format("Source directory does not exist: $0", src_dir));

  Status s = FileExists(env, dest_dir);
  if (!s.ok()) {
    if (create_if_missing) {
      RETURN_NOT_OK_PREPEND(
          env->CreateDir(dest_dir), Format("Cannot create destination directory: $0", dest_dir));
    } else {
      return s.CloneAndPrepend(Format("Destination directory does not exist: $0", dest_dir));
    }
  }

  // Copy files.
  std::vector<string> files;
  RETURN_NOT_OK_PREPEND(
      env->GetChildren(src_dir, &files),
      Format("Cannot get list of files for directory: $0", src_dir));

  for (const string& file : files) {
    if (file != "." && file != "..") {
      const auto src_path = JoinPathSegments(src_dir, file);
      const auto dest_path = JoinPathSegments(dest_dir, file);

      if (use_hard_links) {
        s = env->LinkFile(src_path, dest_path);

        if (s.ok()) {
          continue;
        }
      }

      if (env->DirExists(src_path)) {
        RETURN_NOT_OK_PREPEND(
            CopyDirectory(env, src_path, dest_path, use_hard_links, CreateIfMissing::kTrue),
            Format("Cannot copy directory: $0", src_path));
      } else {
        RETURN_NOT_OK_PREPEND(
            CopyFile(env, src_path, dest_path), Format("Cannot copy file: $0", src_path));
      }
    }
  }

  return Status::OK();
}

}  // namespace yb

#endif  // YB_UTIL_FILE_UTIL_H
