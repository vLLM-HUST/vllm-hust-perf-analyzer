BEGIN;
CREATE TABLE STRING_IDS(id INTEGER PRIMARY KEY, value TEXT);
INSERT INTO STRING_IDS(id, value) VALUES
  (10, 'AI_CORE'),
  (11, 'MODEL_EXECUTE'),
  (12, 'NOTIFY_WAIT'),
  (13, 'MIX_AIC'),
  (14, 'aclmdlRIExecuteAsync'),
  (15, 'aclmdlRICaptureBegin'),
  (16, 'aclmdlRICaptureEnd'),
  (17, 'aclnnGather'),
  (18, 'aclnnMm'),
  (20, 'GatherV2'),
  (21, 'MatMulV2'),
  (30, 'hcclAllGather'),
  (31, 'hcom_allGather_');

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
  (100, 110, 0, 1000, 1, 1, 13, 0, 36, 1, 7),
  (120, 130, 0, 1001, 2, 1, 13, 0, 36, 2, 7),
  (200, 210, 0, 1002, 3, 1, 13, 0, 36, 3, 7),
  (220, 230, 0, 1003, 4, 1, 13, 0, 36, 4, 7),
  (100, 101, 0, 2000, 5, 1, 11, 0, 3, 5, 7),
  (120, 121, 0, 2001, 6, 1, 11, 0, 3, 6, 7),
  (200, 201, 0, 2002, 7, 1, 11, 0, 3, 7, 7),
  (220, 221, 0, 2003, 8, 1, 11, 0, 3, 8, 7);

CREATE TABLE COMPUTE_TASK_INFO(
  globalTaskId INTEGER,
  name INTEGER,
  opType INTEGER,
  taskType INTEGER
);
INSERT INTO COMPUTE_TASK_INFO VALUES
  (1, 20, 20, 13),
  (2, 21, 21, 13),
  (3, 21, 21, 13),
  (4, 21, 21, 13);

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
  (30, 31, 105, 109, 3000, NULL, 1, 0),
  (30, 31, 300, 320, 3001, NULL, 2, 0);

CREATE TABLE CANN_API(
  startNs INTEGER,
  endNs INTEGER,
  type INTEGER,
  globalTid INTEGER,
  connectionId INTEGER,
  name INTEGER
);
INSERT INTO CANN_API VALUES
  (10, 11, 0, 1, 9000, 15),
  (12, 13, 0, 1, 9001, 17),
  (14, 15, 0, 1, 9002, 16),
  (20, 21, 0, 1, 9003, 15),
  (22, 23, 0, 1, 9004, 18),
  (24, 25, 0, 1, 9005, 16),
  (90, 95, 0, 1, 2000, 14),
  (115, 118, 0, 1, 2001, 14),
  (190, 195, 0, 1, 2002, 14),
  (215, 218, 0, 1, 2003, 14);
COMMIT;
