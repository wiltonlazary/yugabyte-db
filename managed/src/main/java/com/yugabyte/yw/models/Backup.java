// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.avaje.ebean.Model;
import com.avaje.ebean.annotation.CreatedTimestamp;
import com.avaje.ebean.annotation.DbJson;
import com.avaje.ebean.annotation.EnumValue;
import com.avaje.ebean.annotation.UpdatedTimestamp;
import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.forms.BackupTableParams;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;

import static java.lang.Math.abs;

@Entity
public class Backup extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(Backup.class);
  SimpleDateFormat tsFormat = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss");

  public enum BackupState {
    @EnumValue("In Progress")
    InProgress,

    @EnumValue("Completed")
    Completed,

    @EnumValue("Failed")
    Failed,

    @EnumValue("Deleted")
    Deleted,

    @EnumValue("Skipped")
    Skipped
  }

  @Id
  public UUID backupUUID;

  @Column(nullable = false)
  public UUID customerUUID;

  @Column(nullable = false)
  public BackupState state;

  @Column(columnDefinition = "TEXT", nullable = false)
  @DbJson
  public JsonNode backupInfo;

  @Column(unique = true)
  public UUID taskUUID;

  public void setBackupInfo(BackupTableParams params) {
    this.backupInfo = Json.toJson(params);
  }

  public BackupTableParams getBackupInfo() {
    return Json.fromJson(this.backupInfo, BackupTableParams.class);
  }

  @CreatedTimestamp
  private Date createTime;
  public Date getCreateTime() { return createTime; }

  @UpdatedTimestamp
  private Date updateTime;
  public Date getUpdateTime() { return updateTime; }

  public static final Find<UUID, Backup> find = new Find<UUID, Backup>(){};

  // For creating new backup we would set the storage location based on
  // universe UUID and backup UUID.
  // univ-<univ_uuid>/backup-<timestamp>-<something_to_disambiguate_from_yugaware>/table-keyspace.table_name.table_uuid
  private void updateStorageLocation(BackupTableParams params) {
    CustomerConfig customerConfig = CustomerConfig.get(customerUUID, params.storageConfigUUID);
    params.storageLocation = String.format("univ-%s/backup-%s-%d/table-%s.%s",
        params.universeUUID, tsFormat.format(new Date()), abs(backupUUID.hashCode()),
        params.keyspace, params.tableName);
    if (params.tableUUID != null) {
      params.storageLocation = String.format("%s-%s",
          params.storageLocation,
          params.tableUUID.toString().replace("-", "")
      );
    }
    if (customerConfig != null) {
      JsonNode storageNode = null;
      // TODO: These values, S3 vs NFS / S3_BUCKET vs NFS_PATH come from UI right now...
      if (customerConfig.name.equals("S3")) {
        storageNode = customerConfig.getData().get("S3_BUCKET");
      } else if (customerConfig.name.equals("NFS")) {
        storageNode = customerConfig.getData().get("NFS_PATH");
      }
      if (storageNode != null) {
        String storagePath = storageNode.asText();
        if (storagePath != null && !storagePath.isEmpty()) {
          params.storageLocation = String.format("%s/%s", storagePath, params.storageLocation);
        }
      }
    }
  }

  public static Backup create(UUID customerUUID, BackupTableParams params) {
    Backup backup = new Backup();
    backup.backupUUID = UUID.randomUUID();
    backup.customerUUID = customerUUID;
    backup.state = BackupState.InProgress;
    if (params.storageLocation == null) {
      // We would derive the storage location based on the parameters
      backup.updateStorageLocation(params);
    }
    backup.setBackupInfo(params);
    backup.save();
    return backup;
  }

  // We need to set the taskUUID right after commissioner task is submitted.
  public synchronized void setTaskUUID(UUID taskUUID) {
    if (this.taskUUID == null) {
      this.taskUUID = taskUUID;
      save();
    }
  }

  public static List<Backup> fetchByUniverseUUID(UUID customerUUID, UUID universeUUID) {
      List<Backup> backupList = find.where().eq("customer_uuid", customerUUID).orderBy("create_time desc").findList();
      return backupList.stream()
          .filter(backup -> backup.getBackupInfo().universeUUID.equals(universeUUID))
          .collect(Collectors.toList());
  }

  public static Backup get(UUID customerUUID, UUID backupUUID) {
    return find.where().idEq(backupUUID).eq("customer_uuid", customerUUID).findUnique();
  }

  public static Backup fetchByTaskUUID(UUID taskUUID) {
    return Backup.find.where().eq("task_uuid", taskUUID).findUnique();
  }

  public void transitionState(BackupState newState) {
    // We only allow state transition from InProgress to a valid state
    // Or completed to deleted state.
    if ((this.state == BackupState.InProgress && this.state != newState) ||
        (this.state == BackupState.Completed && newState == BackupState.Deleted)) {
      this.state = newState;
      save();
    }
  }
}
