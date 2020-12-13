// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Preconditions;
import io.ebean.Finder;
import io.ebean.FutureRowCount;
import io.ebean.Model;
import io.ebean.RawSql;
import io.ebean.Ebean;
import io.ebean.RawSqlBuilder;
import io.ebean.Query;
import io.ebean.annotation.EnumValue;
import io.ebean.annotation.Transactional;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;

import javax.persistence.*;
import java.lang.reflect.Field;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.TemporalUnit;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.stream.Collectors;

@Entity
public class CustomerTask extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(CustomerTask.class);

  public enum TargetType {
    @EnumValue("Universe")
    Universe,

    @EnumValue("Cluster")
    Cluster,

    @EnumValue("Table")
    Table,

    @EnumValue("Provider")
    Provider,

    @EnumValue("Node")
    Node,

    @EnumValue("Backup")
    Backup,

    @EnumValue("KMS Configuration")
    KMSConfiguration,
  }

  public enum TaskType {
    @EnumValue("Create")
    Create,

    @EnumValue("Update")
    Update,

    @EnumValue("Delete")
    Delete,

    @EnumValue("Stop")
    Stop,

    @EnumValue("Start")
    Start,

    @EnumValue("Restart")
    Restart,

    @EnumValue("Remove")
    Remove,

    @EnumValue("Add")
    Add,

    @EnumValue("Release")
    Release,

    @EnumValue("UpgradeSoftware")
    UpgradeSoftware,

    @EnumValue("UpdateDiskSize")
    UpdateDiskSize,

    @EnumValue("UpgradeGflags")
    UpgradeGflags,

    @EnumValue("BulkImportData")
    BulkImportData,

    @Deprecated
    @EnumValue("Backup")
    Backup,

    @EnumValue("Restore")
    Restore,

    @Deprecated
    @EnumValue("SetEncryptionKey")
    SetEncryptionKey,

    @EnumValue("EnableEncryptionAtRest")
    EnableEncryptionAtRest,

    @EnumValue("RotateEncryptionKey")
    RotateEncryptionKey,

    @EnumValue("DisableEncryptionAtRest")
    DisableEncryptionAtRest,

    @EnumValue("StartMaster")
    StartMaster;

    public String toString(boolean completed) {
      switch (this) {
        case Create:
          return completed ? "Created " : "Creating ";
        case Update:
          return completed ? "Updated " : "Updating ";
        case Delete:
          return completed ? "Deleted " : "Deleting ";
        case UpgradeSoftware:
          return completed ? "Upgraded Software " : "Upgrading Software ";
        case UpgradeGflags:
          return completed ? "Upgraded GFlags " : "Upgrading GFlags ";
        case BulkImportData:
          return completed ? "Bulk imported data" : "Bulk importing data";
        case Restore:
          return completed ? "Restored " : "Restoring ";
        case Restart:
          return completed ? "Restarted " : "Restarting ";
        case Backup:
          return completed ? "Backed up " : "Backing up ";
        case SetEncryptionKey:
          return completed ? "Set encryption key" : "Setting encryption key";
        case EnableEncryptionAtRest:
          return completed ? "Enabled encryption at rest" : "Enabling encryption at rest";
        case RotateEncryptionKey:
          return completed ? "Rotated encryption at rest universe key" :
            "Rotating encryption at rest universe key";
        case DisableEncryptionAtRest:
          return completed ? "Disabled encryption at rest" : "Disabling encryption at rest";
        case StartMaster:
          return completed ? "Started Master process on " : "Starting Master process on ";
        default:
          return null;
      }
    }

    public static List<TaskType> filteredValues() {
      return Arrays.stream(TaskType.values()).filter(value -> {
        try {
          Field field = TaskType.class.getField(value.name());
          return !field.isAnnotationPresent(Deprecated.class);
        } catch (Exception e) {
          return false;
        }
      }).collect(Collectors.toList());
    }

    public String getFriendlyName() {
      switch (this) {
        case StartMaster:
          return "Start Master Process on";
        default:
          return name();
      }
    }
  }

  @Id
  @SequenceGenerator(
    name = "customer_task_id_seq", sequenceName = "customer_task_id_seq", allocationSize = 1)
  @GeneratedValue(strategy = GenerationType.SEQUENCE, generator = "customer_task_id_seq")
  private Long id;

  @Constraints.Required
  @Column(nullable = false)
  private UUID customerUUID;

  public UUID getCustomerUUID() {
    return customerUUID;
  }

  @Constraints.Required
  @Column(nullable = false)
  private UUID taskUUID;

  public UUID getTaskUUID() {
    return taskUUID;
  }

  @Constraints.Required
  @Column(nullable = false)
  private TargetType targetType;

  public TargetType getTarget() {
    return targetType;
  }

  @Constraints.Required
  @Column(nullable = false)
  private String targetName;

  public String getTargetName() {
    return targetName;
  }

  @Constraints.Required
  @Column(nullable = false)
  private TaskType type;

  public TaskType getType() {
    return type;
  }

  @Constraints.Required
  @Column(nullable = false)
  private UUID targetUUID;

  public UUID getTargetUUID() {
    return targetUUID;
  }

  @Constraints.Required
  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  private Date createTime;

  public Date getCreateTime() {
    return createTime;
  }

  @Column
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  private Date completionTime;

  public Date getCompletionTime() {
    return completionTime;
  }

  public void markAsCompleted() {
    markAsCompleted(new Date());
  }

  @VisibleForTesting
  void markAsCompleted(Date completionTime) {
    if (this.completionTime == null) {
      this.completionTime = completionTime;
      this.save();
    }
  }

  public static final Finder<Long, CustomerTask> find =
    new Finder<Long, CustomerTask>(CustomerTask.class) {
    };

  public static CustomerTask create(Customer customer, UUID targetUUID, UUID taskUUID,
                                    TargetType targetType, TaskType type, String targetName) {
    CustomerTask th = new CustomerTask();
    th.customerUUID = customer.uuid;
    th.targetUUID = targetUUID;
    th.taskUUID = taskUUID;
    th.targetType = targetType;
    th.type = type;
    th.targetName = targetName;
    th.createTime = new Date();
    th.save();
    return th;
  }

  public static CustomerTask get(Long id) {
    return CustomerTask.find.query().where()
      .idEq(id).findOne();
  }

  public static CustomerTask get(UUID customerUUID, UUID taskUUID) {
    return CustomerTask.find.query().where()
    .eq("customer_uuid", customerUUID)
    .eq("task_uuid", taskUUID)
    .findOne();
  }

  public String getFriendlyDescription() {
    StringBuilder sb = new StringBuilder();
    sb.append(type.toString(completionTime != null));
    sb.append(targetType.name());
    sb.append(" : ").append(targetName);
    return sb.toString();
  }

  /**
   * deletes customer_task, task_info and all its subtasks of a given task.
   * Assumes task_info tree is one level deep. If this assumption changes then
   * this code needs to be reworked to recurse.
   * When successful; it deletes at least 2 rows because there is always
   * customer_task and associated task_info row that get deleted.
   *
   * @return number of rows deleted.
   * ==0 - if deletion was skipped due to data integrity issues.
   * >=2 - number of rows deleted
   */
  @Transactional
  public int cascadeDeleteCompleted() {
    Preconditions.checkNotNull(completionTime,
      String.format("CustomerTask %s has not completed", id));
    TaskInfo rootTaskInfo = TaskInfo.get(taskUUID);
    if (!rootTaskInfo.hasCompleted()) {
      LOG.warn("Completed CustomerTask(id:{}, type:{}) has incomplete task_info {}",
        id, type, rootTaskInfo);
      return 0;
    }
    List<TaskInfo> subTasks = rootTaskInfo.getSubTasks();
    List<TaskInfo> incompleteSubTasks = subTasks.stream()
      .filter(taskInfo -> !taskInfo.hasCompleted())
      .collect(Collectors.toList());
    if (rootTaskInfo.getTaskState() == TaskInfo.State.Success && !incompleteSubTasks.isEmpty()) {
      LOG.warn(
        "For a customer_task.id: {}, Successful task_info.uuid ({}) has {} incomplete subtasks {}",
        id, rootTaskInfo.getTaskUUID(), incompleteSubTasks.size(), incompleteSubTasks);
      return 0;
    }
    // Note: delete leaf nodes first to preserve referential integrity.
    subTasks.forEach(Model::delete);
    rootTaskInfo.delete();
    this.delete();
    return 2 + subTasks.size();
  }

  public static CustomerTask findByTaskUUID(UUID taskUUID) {
    return find.query().where().eq("task_uuid", taskUUID).findOne();
  }

  public static List<CustomerTask> findOlderThan(Customer customer, Duration duration) {
    Date cutoffDate = new Date(Instant.now().minus(duration).toEpochMilli());
    return find.query().where()
      .eq("customerUUID", customer.uuid)
      .le("completion_time", cutoffDate)
      .findList();
  }

  public static List<CustomerTask> findIncompleteByTargetUUID(UUID targetUUID) {
    return findIncompleteCustomerTargetTasks(null, targetUUID);
  }

  public static List<CustomerTask> findIncompleteCustomerTargetTasks(UUID customerUUID,
                                                                     UUID targetUUID) {
    Query<CustomerTask> query = find.query().where()
      .isNull("completion_time")
      .orderBy("create_time desc");
    if (customerUUID != null) {
      query.where().eq("customer_uuid", customerUUID);
    }
    if (targetUUID != null) {
      query.where().eq("target_uuid", customerUUID);
    }
    return query.findList();
  }

  public static List<CustomerTask> findCustomerTasks(UUID customerUUID, UUID targetUUID,
                                                     int offset, int limit) {
    Query<CustomerTask> customerTaskQuery = find.query().where()
      .eq("customer_uuid", customerUUID)
      .orderBy("create_time desc")
      .setFirstRow(offset)
      .setMaxRows(limit);

    if (targetUUID != null) {
      customerTaskQuery.where().eq("target_uuid", targetUUID);
    }
    return customerTaskQuery.findList();
  }

  public static int countCustomerTasks(UUID customerUUID) {
    return find.query().where()
      .eq("customer_uuid", customerUUID)
      .findCount();
  }

  public static List<CustomerTask> findCustomerAllUniversesIncompleteTasks(UUID customerUUID) {
    String sql = String.format("SELECT customer_uuid, task_uuid, target_type, target_name,"
        + " type, target_uuid, completion_time"
        + " FROM customer_task INNER JOIN universe"
        + " ON universe_uuid= target_uuid"
        + " WHERE customer_uuid = '%s' and completion_time IS NULL"
        + " order by create_time desc",
      customerUUID.toString());
    RawSql rawSql = RawSqlBuilder.parse(sql)
      .columnMapping("customer_uuid", "customerUUID")
      .columnMapping("task_uuid", "taskUUID")
      .columnMapping("target_uuid", "targetUUID")
      .create();

    Query<CustomerTask> query = Ebean.find(CustomerTask.class);
    query.setRawSql(rawSql);
    return query.findList();
  }

  public static CustomerTask getLatestByUniverseUuid(UUID universeUUID) {
    List<CustomerTask> tasks = find.query().where()
      .eq("target_uuid", universeUUID)
      .isNotNull("completion_time")
      .orderBy("completion_time desc")
      .setMaxRows(1)
      .findList();
    if (tasks.size() > 0) {
      return tasks.get(0);
    } else {
      return null;
    }
  }

  public String getNotificationTargetName() {
    if (getType().equals(TaskType.Create) && getTarget().equals(TargetType.Backup)) {
      return Universe.get(getTargetUUID()).name;
    } else {
      return getTargetName();
    }
  }
}
