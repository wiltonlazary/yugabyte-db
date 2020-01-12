// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import java.util.Date;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import java.util.List;
import java.util.Arrays;
import java.util.stream.Collectors;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Enumerated;
import javax.persistence.EnumType;
import javax.persistence.GeneratedValue;
import javax.persistence.GenerationType;
import javax.persistence.Id;

import org.joda.time.DateTime;
import org.mindrot.jbcrypt.BCrypt;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.avaje.ebean.annotation.DbJson;
import com.avaje.ebean.annotation.EnumValue;
import com.avaje.ebean.Model;
import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.base.Joiner;

import play.data.validation.Constraints;
import play.libs.Json;


@Entity
public class Users extends Model {

  public static final Logger LOG = LoggerFactory.getLogger(Users.class);
  // A globally unique UUID for the Users.

  /**
   * These are the various states of the task and taskgroup.
   */
  public enum Role {
    @EnumValue("Admin")
    Admin,

    @EnumValue("ReadOnly")
    ReadOnly,
  }


  @Id
  @Column(nullable = false, unique = true)
  public UUID uuid = UUID.randomUUID();

  @Column(nullable = false)
  public UUID customerUUID;

  public void setCustomerUuid(UUID id) {
    this.customerUUID = id;
  }

  @Column(length = 256, unique = true, nullable = false)
  @Constraints.Required
  @Constraints.Email
  public String email;

  public String getEmail() {
    return this.email;
  }

  @Column(length = 256, nullable = false)
  public String passwordHash;

  public void setPassword(String password) {
    this.passwordHash = BCrypt.hashpw(password, BCrypt.gensalt());
  }

  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  public Date creationDate;

  private String authToken;

  @Column(nullable = true)
  private Date authTokenIssueDate;

  @Column(nullable = true)
  private String apiToken;

  @Column(nullable = true, columnDefinition = "TEXT")
  private JsonNode features;

  // The role of the user.
  @Column(nullable = false)
  @Enumerated(EnumType.STRING)
  private Role role;
  public Role getRole() {
    return this.role;
  }
  public void setRole(Role role) {
    this.role = role;
  }

  @Column(nullable = false)
  private boolean isPrimary;
  public boolean getIsPrimary() {
    return this.isPrimary;
  }
  public void setIsPrimary(boolean isPrimary) {
    this.isPrimary = isPrimary;
  }

  public Date getAuthTokenIssueDate() {
    return this.authTokenIssueDate;
  }

  public static final Find<UUID, Users> find = new Find<UUID, Users>() {
  };

  public static Users get(UUID userUUID) {
    return find.where().eq("uuid", userUUID).findUnique();
  }

  public static List<Users> getAll(UUID customerUUID) {
    return find.where().eq("customer_uuid", customerUUID).findList();
  }

  public Users() {
    this.creationDate = new Date();
  }

  public static Users create(String email, String password, Role role, UUID customerUUID) {
    return Users.create(email, password, role, customerUUID, false);
  }

  /**
   * Create new Users, we encrypt the password before we store it in the DB
   *
   * @param name
   * @param email
   * @param password
   * @return Newly Created Users
   */
  public static Users create(String email, String password, Role role, UUID customerUUID,
                             boolean isPrimary) {
    Users users = new Users();
    users.email = email.toLowerCase();
    users.setPassword(password);
    users.setCustomerUuid(customerUUID);
    users.creationDate = new Date();
    users.role = role;
    users.isPrimary = isPrimary;
    users.save();
    return users;
  }

  /**
   * Validate if the email and password combination is valid, we use this to authenticate
   * the Users.
   *
   * @param email
   * @param password
   * @return Authenticated Users Info
   */
  public static Users authWithPassword(String email, String password) {
    Users users = Users.find.where().eq("email", email).findUnique();

    if (users != null && BCrypt.checkpw(password, users.passwordHash)) {
      return users;
    } else {
      return null;
    }
  }

  /**
   * Create a random auth token for the Users and store it in the DB.
   *
   * @return authToken
   */
  public String createAuthToken() {
    Date tokenExpiryDate = new DateTime().minusDays(1).toDate();
    if (authTokenIssueDate == null || authTokenIssueDate.before(tokenExpiryDate)) {
      authToken = UUID.randomUUID().toString();
      authTokenIssueDate = new Date();
      save();
    }
    return authToken;
  }

  public void setAuthToken(String authToken) {
    this.authToken = authToken;
    save();
  }

  /**
   * Create a random auth token without expiry date for Users and store it in the DB.
   *
   * @return apiToken
   */
  public String upsertApiToken() {
    apiToken = UUID.randomUUID().toString();
    save();
    return apiToken;
  }

  /**
   * Get current apiToken.
   *
   * @return apiToken
   */
  public String getApiToken() {
    if (apiToken == null) {
      return null;
    }
    return apiToken.toString();
  }

  /**
   * Authenticate with Token, would check if the authToken is valid.
   *
   * @param authToken
   * @return Authenticated Users Info
   */
  public static Users authWithToken(String authToken) {
    if (authToken == null) {
      return null;
    }

    try {
      // TODO: handle authToken expiry etc.
      return find.where().eq("authToken", authToken).findUnique();
    } catch (Exception e) {
      return null;
    }
  }

  /**
   * Authenticate with API Token, would check if apiToken is valid.
   *
   * @param apiToken
   * @return Authenticated Users Info
   */
  public static Users authWithApiToken(String apiToken) {
    if (apiToken == null) {
      return null;
    }

    try {
      return find.where().eq("apiToken", apiToken).findUnique();
    } catch (Exception e) {
      return null;
    }
  }

  /**
   * Delete authToken for the Users.
   */
  public void deleteAuthToken() {
    authToken = null;
    authTokenIssueDate = null;
    save();
  }

  /**
   * Get features for this Users.
   */
  public JsonNode getFeatures() {
    return features == null ? Json.newObject() : features;
  }

  /**
   * Upserts features for this Users. If updating a feature, only specified features will
   * be updated.
   */
  public void upsertFeatures(JsonNode input) {
    if (features == null) {
      features = input;
    } else {
      ((ObjectNode) features).setAll((ObjectNode) input);
    }
    save();
  }

  public static String getAllEmailsForCustomer(UUID customerUUID) {
    List<Users> users = Users.getAll(customerUUID);
    return users.stream().map(user -> user.email).collect(Collectors.joining(","));
  }

  public static List<Users> getAllReadOnly() {
    return find.where().eq("role", Role.ReadOnly).findList();
  }
}
