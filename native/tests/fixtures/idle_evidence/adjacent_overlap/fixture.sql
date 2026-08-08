-- Golden fixture class: adjacent_overlap.
-- Device-control states overlap inside one visible productive gap.  E4 must
-- split at every boundary and obey wait > capture > record priority without
-- losing the exact TASK lineage.

CREATE TABLE STRING_IDS(id INTEGER PRIMARY KEY, value TEXT);
INSERT INTO STRING_IDS(id, value) VALUES
  (10, 'AI_CORE'),
  (11, 'MIX_AIC'),
  (12, 'EVENT_WAIT'),
  (13, 'CAPTURE_WAIT'),
  (14, 'EVENT_RECORD'),
  (20, 'matmul_fixture'),
  (21, 'MatMul');

CREATE TABLE TASK(
  startNs INTEGER, endNs INTEGER, deviceId INTEGER, connectionId INTEGER,
  globalTaskId INTEGER, globalPid INTEGER, taskType INTEGER, contextId INTEGER,
  streamId INTEGER, taskId INTEGER, modelId INTEGER);
INSERT INTO TASK(startNs, endNs, deviceId, connectionId, globalTaskId,
                 globalPid, taskType, contextId, streamId, taskId, modelId)
VALUES
  (1000, 2000, 0, 101, 1001, 1, 10, 0, 3, 1, 1),
  (2500, 3500, 0, 102, 1002, 1, 12, 0, 4, 2, 1),
  (3200, 4200, 0, 103, 1003, 1, 13, 0, 5, 3, 1),
  (4000, 4500, 0, 104, 1004, 1, 14, 0, 6, 4, 1),
  (5000, 6000, 0, 105, 1005, 1, 10, 0, 3, 5, 1);

CREATE TABLE COMPUTE_TASK_INFO(globalTaskId INTEGER, name INTEGER,
                               opType INTEGER, taskType INTEGER);
INSERT INTO COMPUTE_TASK_INFO(globalTaskId, name, opType, taskType) VALUES
  (1001, 20, 21, 11),
  (1005, 20, 21, 11);

CREATE TABLE CANN_API(
  startNs INTEGER, endNs INTEGER, type INTEGER, globalTid INTEGER,
  connectionId INTEGER, name INTEGER);
