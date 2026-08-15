BEGIN;
CREATE TABLE STRING_IDS(id INTEGER PRIMARY KEY, value TEXT);
INSERT INTO STRING_IDS(id, value) VALUES
  (10, 'AI_CORE'),
  (11, 'EVENT_WAIT'),
  (20, 'model.layers.0.mlp.gate_up_proj'),
  (21, 'MatMul'),
  (22, 'MIX_AIC'),
  (30, 'hcclAllReduce'),
  (31, 'hcom_allReduce_'),
  (40, 'comm_task_hccl_allreduce');

CREATE TABLE TASK(
  startNs INTEGER,
  endNs INTEGER,
  deviceId INTEGER,
  connectionId INTEGER,
  globalTaskId INTEGER,
  globalPid INTEGER,
  taskType INTEGER,
  contextId INTEGER,
  streamId INTEGER,
  taskId INTEGER,
  modelId INTEGER
);
INSERT INTO TASK VALUES
  (100, 160, 0, 700, 9001, 1, 10, 0, 3, 99, 2),
  (170, 210, 0, 701, 9002, 1, 11, 0, 3, 100, 2);

CREATE TABLE COMPUTE_TASK_INFO(
  globalTaskId INTEGER,
  name INTEGER,
  opType INTEGER,
  taskType INTEGER
);
INSERT INTO COMPUTE_TASK_INFO VALUES (9001, 20, 21, 22);

CREATE TABLE COMMUNICATION_TASK_INFO(
  globalTaskId INTEGER,
  name INTEGER,
  taskType INTEGER
);
INSERT INTO COMMUNICATION_TASK_INFO VALUES (9002, 40, 31);

CREATE TABLE COMMUNICATION_OP(
  opName INTEGER,
  opType INTEGER,
  startNs INTEGER,
  endNs INTEGER,
  connectionId INTEGER,
  groupName INTEGER,
  opId INTEGER,
  deviceId INTEGER
);
INSERT INTO COMMUNICATION_OP VALUES
  (30, 31, 165, 215, 701, NULL, 55, 0);
COMMIT;
