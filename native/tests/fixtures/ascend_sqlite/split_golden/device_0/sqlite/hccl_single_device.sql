PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE HCCLTaskSingleDevice(stream_id INTEGER, task_id INTEGER, context_id INTEGER, op_name TEXT, hccl_name TEXT);
INSERT INTO HCCLTaskSingleDevice VALUES(4,101,0,'hcom_allReduce__golden_0','hcom_allReduce_');
CREATE TABLE HCCLOpSingleDevice(op_name TEXT, op_type TEXT, timestamp NUMERIC, connection_id INTEGER);
INSERT INTO HCCLOpSingleDevice VALUES('hcom_allReduce_','hcom_allReduce_',9000,702);
COMMIT;
