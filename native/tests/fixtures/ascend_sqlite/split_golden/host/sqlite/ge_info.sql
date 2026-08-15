PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE TaskInfo(model_id INTEGER, op_name TEXT, stream_id INTEGER, task_id INTEGER, task_type TEXT, op_type TEXT, index_id INTEGER, device_id INTEGER, context_id INTEGER);
INSERT INTO TaskInfo VALUES(2,'linear',3,99,'MIX_AIC','MatMul',-1,0,0);
COMMIT;
