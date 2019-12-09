package com.yugabyte.yw.common.kms.util;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.models.Customer;
import java.util.Base64;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.mockito.InjectMocks;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import play.libs.Json;

public class EncryptionAtRestUtilTest extends FakeDBApplication {
    Customer testCustomer;

    @InjectMocks
    EncryptionAtRestUtil encryptionUtil;

    @Before
    public void setup() {
        testCustomer = ModelFactory.testCustomer();
    }

    @Test
    public void testGenerateSalt() {
        String salt = encryptionUtil
                .generateSalt(testCustomer.uuid, KeyProvider.SMARTKEY);
        assertNotNull(salt);
    }

    @Test
    public void testMaskAndUnmaskConfigData() {
        JsonNode originalObj = Json.newObject().put("test_key", "test_val");
        ObjectNode encryptedObj = encryptionUtil.maskConfigData(
                testCustomer.uuid,
                originalObj,
                KeyProvider.SMARTKEY
        );
        JsonNode unencryptedObj = encryptionUtil.unmaskConfigData(
                testCustomer.uuid,
                encryptedObj,
                KeyProvider.SMARTKEY
        );
        assertEquals(originalObj.get("test_key").asText(), unencryptedObj.get("test_key").asText());
    }

    @Test
    public void testGetUniverseKeyCacheEntryNoEntry() {
        assertNull(encryptionUtil.getUniverseKeyCacheEntry(
            UUID.randomUUID(),
            new String("some_key_ref").getBytes())
        );
    }

    @Test
    public void testSetAndGetUniverseKeyCacheEntry() {
        UUID universeUUID = UUID.randomUUID();
        byte[] keyRef = new String("some_key_ref").getBytes();
        byte[] keyVal = new String("some_key_val").getBytes();
        encryptionUtil.setUniverseKeyCacheEntry(universeUUID, keyRef, keyVal);
        assertEquals(
                Base64.getEncoder().encodeToString(keyVal),
                Base64.getEncoder().encodeToString(
                        encryptionUtil.getUniverseKeyCacheEntry(universeUUID, keyRef)
                )
        );
    }

    @Test
    public void testSetUpdateAndGetUniverseKeyCacheEntry() {
        UUID universeUUID = UUID.randomUUID();
        byte[] keyRef = new String("some_key_ref").getBytes();
        byte[] keyVal = new String("some_key_val").getBytes();
        encryptionUtil.setUniverseKeyCacheEntry(universeUUID, keyRef, keyVal);
        assertEquals(
                Base64.getEncoder().encodeToString(keyVal),
                Base64.getEncoder().encodeToString(
                        encryptionUtil.getUniverseKeyCacheEntry(universeUUID, keyRef)
                )
        );
        keyVal = new String("some_new_key_val").getBytes();
        encryptionUtil.setUniverseKeyCacheEntry(universeUUID, keyRef, keyVal);
        assertEquals(
                Base64.getEncoder().encodeToString(keyVal),
                Base64.getEncoder().encodeToString(
                        encryptionUtil.getUniverseKeyCacheEntry(universeUUID, keyRef)
                )
        );
    }

    @Test
    public void testClearUniverseKeyCacheEntry() {
        UUID universeUUID = UUID.randomUUID();
        byte[] keyRef = new String("some_key_ref").getBytes();
        byte[] keyVal = new String("some_key_val").getBytes();
        encryptionUtil.setUniverseKeyCacheEntry(universeUUID, keyRef, keyVal);
        assertEquals(
                Base64.getEncoder().encodeToString(keyVal),
                Base64.getEncoder().encodeToString(
                        encryptionUtil.getUniverseKeyCacheEntry(universeUUID, keyRef)
                )
        );
        encryptionUtil.removeUniverseKeyCacheEntry(universeUUID);
        assertNull(encryptionUtil.getUniverseKeyCacheEntry(universeUUID, keyRef));
    }
}
