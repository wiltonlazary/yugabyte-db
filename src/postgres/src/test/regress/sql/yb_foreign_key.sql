--
-- FOREIGN KEY (YB-added tests)
--
-- TODO: Run this test with REPEATABLE READ isolation level:
--       https://github.com/yugabyte/yugabyte-db/issues/2604
--
-- MATCH FULL
--
-- First test, check and cascade
--


CREATE TABLE ITABLE ( ptest1 int, ptest2 text );
CREATE UNIQUE INDEX ITABLE_IDX ON ITABLE(ptest1);
CREATE TABLE FKTABLE ( ftest1 int REFERENCES ITABLE(ptest1) MATCH FULL ON DELETE CASCADE ON UPDATE CASCADE, ftest2 int );

-- Insert test data into ITABLE
INSERT INTO ITABLE VALUES (1, 'Test1');
INSERT INTO ITABLE VALUES (2, 'Test2');
INSERT INTO ITABLE VALUES (3, 'Test3');
INSERT INTO ITABLE VALUES (4, 'Test4');
INSERT INTO ITABLE VALUES (5, 'Test5');

-- Insert successful rows into FK TABLE
INSERT INTO FKTABLE VALUES (1, 2);
INSERT INTO FKTABLE VALUES (2, 3);
INSERT INTO FKTABLE VALUES (3, 4);
INSERT INTO FKTABLE VALUES (NULL, 1);

-- Insert a failed row into FK TABLE
INSERT INTO FKTABLE VALUES (100, 2);

-- Check FKTABLE
SELECT * FROM FKTABLE ORDER BY ftest1;

-- Delete a row from ITABLE
DELETE FROM ITABLE WHERE ptest1=1;

-- Check FKTABLE for removal of matched row
SELECT * FROM FKTABLE ORDER BY ftest1;

-- Update a row from ITABLE
UPDATE ITABLE SET ptest1=1 WHERE ptest1=2;

-- Check FKTABLE for update of matched row
SELECT * FROM FKTABLE ORDER BY ftest1;

DROP TABLE FKTABLE;
DROP TABLE ITABLE;

--
-- check set NULL and table constraint on multiple columns
--
CREATE TABLE ITABLE ( ptest1 int, ptest2 int, ptest3 text);
CREATE UNIQUE INDEX ITABLE_IDX ON ITABLE(ptest1, ptest2);

CREATE TABLE FKTABLE ( ftest1 int, ftest2 int, ftest3 int, CONSTRAINT constrname FOREIGN KEY(ftest1, ftest2)
                       REFERENCES ITABLE(ptest1, ptest2) MATCH FULL ON DELETE SET NULL ON UPDATE SET NULL);


-- Insert test data into ITABLE
INSERT INTO ITABLE VALUES (1, 2, 'Test1');
INSERT INTO ITABLE VALUES (1, 3, 'Test1-2');
INSERT INTO ITABLE VALUES (2, 4, 'Test2');
INSERT INTO ITABLE VALUES (3, 6, 'Test3');
INSERT INTO ITABLE VALUES (4, 8, 'Test4');
INSERT INTO ITABLE VALUES (5, 10, 'Test5');

-- Insert successful rows into FK TABLE
INSERT INTO FKTABLE VALUES (1, 2, 4);
INSERT INTO FKTABLE VALUES (1, 3, 5);
INSERT INTO FKTABLE VALUES (2, 4, 8);
INSERT INTO FKTABLE VALUES (3, 6, 12);
INSERT INTO FKTABLE VALUES (NULL, NULL, 0);

-- Insert failed rows into FK TABLE
INSERT INTO FKTABLE VALUES (100, 2, 4);
INSERT INTO FKTABLE VALUES (2, 2, 4);
INSERT INTO FKTABLE VALUES (NULL, 2, 4);
INSERT INTO FKTABLE VALUES (1, NULL, 4);

-- Check FKTABLE
SELECT * FROM FKTABLE ORDER BY ftest1, ftest2, ftest3;

-- Delete a row from ITABLE
DELETE FROM ITABLE WHERE ptest1=1 and ptest2=2;

-- Check that values of matched row are set to null.
SELECT * FROM FKTABLE ORDER BY ftest1, ftest2, ftest3;

-- Delete another row from ITABLE
DELETE FROM ITABLE WHERE ptest1=5 and ptest2=10;

-- Check FKTABLE (should be no change)
SELECT * FROM FKTABLE ORDER BY ftest1, ftest2, ftest3;

-- Update a row from ITABLE
UPDATE ITABLE SET ptest1=1 WHERE ptest1=2;

-- Check FKTABLE for update of matched row
SELECT * FROM FKTABLE ORDER BY ftest1, ftest2, ftest3;

-- Test truncate

-- Should fail due to dependency on FKTABLE.
TRUNCATE ITABLE;

-- Should truncate both ITABLE and FKTABLE.
TRUNCATE ITABLE CASCADE;

-- Check that both tables are now empty.
SELECT * FROM ITABLE ORDER BY ptest1, ptest2;
SELECT * FROM FKTABLE ORDER BY ftest1, ftest2, ftest3;

DROP TABLE ITABLE CASCADE;
DROP TABLE FKTABLE;
