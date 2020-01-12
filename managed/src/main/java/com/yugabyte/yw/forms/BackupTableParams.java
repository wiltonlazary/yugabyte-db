// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

import java.util.UUID;

public class BackupTableParams extends TableManagerParams {
  public enum ActionType {
    CREATE,
    RESTORE
  }

  @Constraints.Required
  public UUID storageConfigUUID;

  // Specifies the backup storage location in case of S3 it would have
  // the S3 url based on universeUUID and timestamp.
  public String storageLocation;

  @Constraints.Required
  public ActionType actionType;

  // Specifies the frequency for running the backup in milliseconds.
  public long schedulingFrequency = 0L;

  // Specifies the cron expression in case a recurring backup is expected.
  public String cronExpression = null;

  // Specifies the time before deleting the backup from the storage
  // bucket.
  public long timeBeforeDelete = 0L;

  // Should backup script enable verbose logging.
  public boolean enableVerboseLogs = false;
}
