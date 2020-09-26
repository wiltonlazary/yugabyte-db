// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.common.ModelFactory;
import org.junit.Before;
import org.junit.Test;

import com.yugabyte.yw.common.FakeDBApplication;

import java.util.Map;
import java.util.UUID;

import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.hamcrest.CoreMatchers.*;
import static org.hamcrest.core.StringContains.containsString;
import static org.junit.Assert.*;

public class ProviderTest extends FakeDBApplication {
  private Customer defaultCustomer;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
  }

  @Test
  public void testCreate() {
    Provider provider = ModelFactory.awsProvider(defaultCustomer);

    assertNotNull(provider.uuid);
    assertEquals(provider.name, "Amazon");
    assertTrue(provider.isActive());
  }

  @Test
  public void testNullConfig() {
    Provider provider = ModelFactory.awsProvider(defaultCustomer);
    assertNotNull(provider.uuid);
    assertTrue(provider.getConfig().isEmpty());
  }

  @Test
  public void testNotNullConfig() {
    Provider provider = Provider.create(defaultCustomer.uuid, Common.CloudType.aws,
        "Amazon", ImmutableMap.of("Foo", "Bar"));
    assertNotNull(provider.uuid);
    assertNotNull(provider.getConfig().toString(), allOf(notNullValue(), equalTo("{Foo=Bar}")));
  }

  @Test
  public void testCreateDuplicateProvider() {
    ModelFactory.awsProvider(defaultCustomer);
    try {
      Provider.create(defaultCustomer.uuid, Common.CloudType.aws, "Amazon");
    } catch (Exception e) {
      assertThat(e.getMessage(), containsString("Unique index or primary key violation:"));
    }
  }

  @Test
  public void testGetMaskedConfigWithSensitiveData() {
    Provider provider = Provider.create(defaultCustomer.uuid, Common.CloudType.aws,
            "Amazon", ImmutableMap.of("AWS_ACCESS_KEY_ID", "BarBarBarBar"));
    assertNotNull(provider.uuid);
    assertValue(provider.getMaskedConfig(), "AWS_ACCESS_KEY_ID", "Ba********ar");
    assertEquals("BarBarBarBar", provider.getConfig().get("AWS_ACCESS_KEY_ID"));
  }

  @Test
  public void testGetMaskedConfigWithoutSensitiveData() {
    Provider provider = Provider.create(defaultCustomer.uuid, Common.CloudType.aws,
        "Amazon", ImmutableMap.of("AWS_ACCESS_ID", "BarBarBarBar"));
    assertNotNull(provider.uuid);
    assertValue(provider.getMaskedConfig(), "AWS_ACCESS_ID", "BarBarBarBar");
    assertEquals("BarBarBarBar", provider.getConfig().get("AWS_ACCESS_ID"));
  }

  @Test
  public void testCreateProviderWithSameName() {
    Provider p1 = ModelFactory.awsProvider(defaultCustomer);
    Provider p2 = Provider.create(UUID.randomUUID(), Common.CloudType.aws, "Amazon");
    assertNotNull(p1);
    assertNotNull(p2);
  }

  @Test
  public void testInactiveProvider() {
    Provider provider = ModelFactory.awsProvider(defaultCustomer);

    assertNotNull(provider.uuid);
    assertEquals(provider.name, "Amazon");
    assertTrue(provider.isActive());

    provider.setActiveFlag(false);
    provider.save();

    Provider fetch = Provider.find.byId(provider.uuid);
    assertFalse(fetch.isActive());
  }

  @Test
  public void testFindProvider() {
    Provider provider = ModelFactory.awsProvider(defaultCustomer);

    assertNotNull(provider.uuid);
    Provider fetch = Provider.find.byId(provider.uuid);
    assertNotNull(fetch);
    assertEquals(fetch.uuid, provider.uuid);
    assertEquals(fetch.name, provider.name);
    assertTrue(fetch.isActive());
    assertEquals(fetch.customerUUID, defaultCustomer.uuid);
  }

  @Test
  public void testGetByNameSuccess() {
    Provider provider = ModelFactory.awsProvider(defaultCustomer);
    Provider fetch = Provider.get(defaultCustomer.uuid, Common.CloudType.aws);
    assertNotNull(fetch);
    assertEquals(fetch.uuid, provider.uuid);
    assertEquals(fetch.name, provider.name);
    assertTrue(fetch.isActive());
    assertEquals(fetch.customerUUID, defaultCustomer.uuid);
  }

  @Test
  public void testGetByNameFailure() {
    Provider.create(defaultCustomer.uuid, Common.CloudType.aws, "Amazon");
    Provider.create(defaultCustomer.uuid, Common.CloudType.gcp, "Amazon");
    try {
      Provider.get(defaultCustomer.uuid, Common.CloudType.aws);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(),
              equalTo("Found 2 providers with name: Amazon")));
    }
  }

  @Test
  public void testCascadeDelete() {
    Provider provider = ModelFactory.awsProvider(defaultCustomer);
    Region region = Region.create(provider, "region-1", "region 1", "ybImage");
    AvailabilityZone.create(region, "zone-1", "zone 1", "subnet-1");
    provider.delete();
    assertEquals(0, Region.find.all().size());
    assertEquals(0, AvailabilityZone.find.all().size());
  }

  @Test
  public void testGetAwsHostedZoneWithData() {
    Provider provider = Provider.create(defaultCustomer.uuid, Common.CloudType.aws, "Amazon",
        ImmutableMap.of("AWS_HOSTED_ZONE_ID", "some_id", "AWS_HOSTED_ZONE_NAME", "some_name"));
    assertNotNull(provider.uuid);
    assertEquals("some_id", provider.getAwsHostedZoneId());
    assertEquals("some_name", provider.getAwsHostedZoneName());
  }

  @Test
  public void testGetAwsHostedZoneWithNoData() {
    Provider provider = Provider.create(defaultCustomer.uuid, Common.CloudType.aws, "Amazon");
    assertNotNull(provider.uuid);
    assertNull(provider.getAwsHostedZoneId());
    assertNull(provider.getAwsHostedZoneId());
  }

  @Test
  public void testGetCloudParamsNoRegions() {
    Provider provider = ModelFactory.gcpProvider(defaultCustomer);
    CloudBootstrap.Params params = provider.getCloudParams();
    assertNotNull(params);
    Map<String, CloudBootstrap.Params.PerRegionMetadata> metadata = params.perRegionMetadata;
    assertNotNull(metadata);
    assertEquals(0, metadata.size());
  }

  @Test
  public void testGetCloudParamsWithRegion() {
    Provider provider = ModelFactory.gcpProvider(defaultCustomer);
    String subnetId = "subnet-1";
    String regionCode = "region-1";
    Region region = Region.create(provider, regionCode, "test region", "default-image");
    AvailabilityZone az = AvailabilityZone.create(region, "az-1", "A Zone", subnetId);
    CloudBootstrap.Params params = provider.getCloudParams();
    assertNotNull(params);
    Map<String, CloudBootstrap.Params.PerRegionMetadata> metadata = params.perRegionMetadata;
    assertNotNull(metadata);
    assertEquals(1, metadata.size());
    CloudBootstrap.Params.PerRegionMetadata data = metadata.get(regionCode);
    assertNotNull(data);
    assertEquals(subnetId, data.subnetId);
  }
}
