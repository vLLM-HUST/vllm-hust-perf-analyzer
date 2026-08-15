PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE HCCLOP(device_id INTEGER, index_id INTEGER, op_name TEXT, op_type TEXT, begin REAL, end REAL, connection_id INTEGER);
INSERT INTO HCCLOP VALUES(0,55,'hcom_allReduce_','hcom_allReduce_',9000.0,9001.0,702);
COMMIT;
