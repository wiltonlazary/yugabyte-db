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
import javax.persistence.GeneratedValue;
import javax.persistence.GenerationType;
import javax.persistence.Id;
import javax.persistence.SequenceGenerator;

import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;

import org.joda.time.DateTime;
import org.mindrot.jbcrypt.BCrypt;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.avaje.ebean.annotation.DbJson;
import com.avaje.ebean.Model;
import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.base.Joiner;

import play.data.validation.Constraints;
import play.libs.Json;


@Entity
public class Customer extends Model {

  public static final Logger LOG = LoggerFactory.getLogger(Customer.class);
  // A globally unique UUID for the customer.
  @Column(nullable = false, unique = true)
  public UUID uuid = UUID.randomUUID();
  public void setUuid(UUID uuid) { this.uuid = uuid;}

  // An auto incrementing, user-friendly id for the customer. Used to compose a db prefix. Currently
  // it is assumed that there is a single instance of the db. The id space for this field may have
  // to be partitioned in case the db is being sharded.
  @Id
  @SequenceGenerator(name="customer_id_seq", sequenceName="customer_id_seq", allocationSize=1)
  @GeneratedValue(strategy = GenerationType.SEQUENCE, generator="customer_id_seq")  private Long id;
  public Long getCustomerId() { return id; }

  @Column(length = 15, nullable = false)
  @Constraints.Required
  public String code;

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

  @Column(length = 256, nullable = false)
  @Constraints.Required
  @Constraints.MinLength(3)
  public String name;

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

  public Date getAuthTokenIssueDate() {
    return this.authTokenIssueDate;
  }

  @Column(columnDefinition = "TEXT", nullable = false)
  private String universeUUIDs = "";

  public synchronized void addUniverseUUID(UUID universeUUID) {
    Set<UUID> universes = getUniverseUUIDs();
    universes.add(universeUUID);
    universeUUIDs = Joiner.on(",").join(universes);
    LOG.debug("New universe list for customer [" + name + "] : " + universeUUIDs);
  }

  public synchronized void removeUniverseUUID(UUID universeUUID) {
    Set<UUID> universes = getUniverseUUIDs();
    universes.remove(universeUUID);
    universeUUIDs = Joiner.on(",").join(universes);
    LOG.debug("New universe list for customer [" + name + "] : " + universeUUIDs);
  }

  public Set<UUID> getUniverseUUIDs() {
    Set<UUID> uuids = new HashSet<UUID>();
    if (!universeUUIDs.isEmpty()) {
      List<String> ids = Arrays.asList(universeUUIDs.split(","));
      for (String id : ids) {
        uuids.add(UUID.fromString(id));
      }
    }
    return uuids;
  }

  @JsonIgnore
  public Set<Universe> getUniverses() {
    if (getUniverseUUIDs().isEmpty()) {
      return new HashSet<>();
    }
    return Universe.get(getUniverseUUIDs());
  }

  @JsonIgnore
  public Set<Universe> getUniversesForProvider(UUID providerUUID) {
    Set<Universe> universesInProvider = getUniverses()
        .stream().filter(u -> checkClusterInProvider(u, providerUUID))
        .collect(Collectors.toSet());
    return universesInProvider;
  }

  private boolean checkClusterInProvider(Universe universe, UUID providerUUID) {
    for (Cluster cluster : universe.getUniverseDetails().clusters) {
      if (cluster.userIntent.provider.equals(providerUUID.toString())) {
        return true;
      }
    }
    return false;
  }

  public static final Find<UUID, Customer> find = new Find<UUID, Customer>() {
  };

  public static Customer get(UUID customerUUID) {
    return find.where().eq("uuid", customerUUID).findUnique();
  }

  public static Customer get(long id) {
    return find.where().idEq(String.valueOf(id)).findUnique();
  }

  public static List<Customer> getAll() {
    return find.findList();
  }

  public Customer() {
    this.creationDate = new Date();
  }

  /**
   * Create new customer, we encrypt the password before we store it in the DB
   *
   * @param name
   * @param email
   * @param password
   * @return Newly Created Customer
   */
  public static Customer create(String code, String name, String email, String password) {
    Customer cust = new Customer();
    cust.email = email.toLowerCase();
    cust.setPassword(password);
    cust.code = code;
    cust.name = name;
    cust.creationDate = new Date();
    cust.save();
    return cust;
  }

  /**
   * Validate if the email and password combination is valid, we use this to authenticate
   * the customer.
   *
   * @param email
   * @param password
   * @return Authenticated Customer Info
   */
  public static Customer authWithPassword(String email, String password) {
    Customer cust = Customer.find.where().eq("email", email).findUnique();

    if (cust != null && BCrypt.checkpw(password, cust.passwordHash)) {
      return cust;
    } else {
      return null;
    }
  }

  /**
   * Create a random auth token for the customer and store it in the DB.
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

  /**
   * Create a random auth token without expiry date for customer and store it in the DB.
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
   * @return Authenticated Customer Info
   */
  public static Customer authWithToken(String authToken) {
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
   * @return Authenticated Customer Info
   */
  public static Customer authWithApiToken(String apiToken) {
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
   * Delete authToken for the customer.
   */
  public void deleteAuthToken() {
    authToken = null;
    authTokenIssueDate = null;
    save();
  }

  /**
   * Get features for this customer.
   */
  public JsonNode getFeatures() {
    return features == null ? Json.newObject() : features;
  }

  /**
   * Upserts features for this customer. If updating a feature, only specified features will
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
}
