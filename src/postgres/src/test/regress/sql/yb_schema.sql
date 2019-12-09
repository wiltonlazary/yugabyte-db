--
-- SCHEMA
--

-- Create 2 schemas with table of the same name in each.
CREATE SCHEMA S1;
CREATE SCHEMA S2;

CREATE TABLE S1.TBL (a1 int PRIMARY KEY);
CREATE TABLE S2.TBL (a2 text PRIMARY KEY);

-- Insert values into the tables and verify both can be queried.
INSERT INTO S1.TBL VALUES (1);
INSERT INTO S2.TBL VALUES ('a');

SELECT * FROM S1.TBL;
SELECT * FROM S2.TBL;

-- Drop one table and verify the other still exists.
DROP TABLE S1.TBL;
SELECT * FROM S2.TBL;

DROP TABLE S2.TBL;
