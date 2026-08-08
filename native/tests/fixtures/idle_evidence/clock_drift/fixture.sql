-- Golden fixture class: clock_drift.
-- CANN_API timestamps are in profiler-host coordinates p.  clock_markers.tsv
-- injects d = 2*p + 1000, so raw timestamps cannot overlap the device gap.

CREATE TABLE STRING_IDS(id INTEGER PRIMARY KEY, value TEXT);
INSERT INTO STRING_IDS(id, value) VALUES
  (10, 'AI_CORE'), (11, 'MIX_AIC'),
  (20, 'matmul_fixture'), (21, 'MatMul'),
  (30, 'aclrtSynchronizeStream');

CREATE TABLE TASK(
  startNs INTEGER, endNs INTEGER, deviceId INTEGER, connectionId INTEGER,
  globalTaskId INTEGER, globalPid INTEGER, taskType INTEGER, contextId INTEGER,
  streamId INTEGER, taskId INTEGER, modelId INTEGER);
INSERT INTO TASK(startNs, endNs, deviceId, connectionId, globalTaskId,
                 globalPid, taskType, contextId, streamId, taskId, modelId)
VALUES
  (21000, 31000, 0, 301, 3001, 1, 10, 0, 3, 1, 1),
  (51000, 61000, 0, 302, 3002, 1, 10, 0, 3, 2, 1);

CREATE TABLE COMPUTE_TASK_INFO(globalTaskId INTEGER, name INTEGER,
                               opType INTEGER, taskType INTEGER);
INSERT INTO COMPUTE_TASK_INFO(globalTaskId, name, opType, taskType) VALUES
  (3001, 20, 21, 11), (3002, 20, 21, 11);

CREATE TABLE CANN_API(
  startNs INTEGER, endNs INTEGER, type INTEGER, globalTid INTEGER,
  connectionId INTEGER, name INTEGER);
-- Maps to device [33000,49000); connection 302 uniquely identifies device 0.
INSERT INTO CANN_API(startNs, endNs, type, globalTid, connectionId, name)
VALUES (16000, 24000, 0, 1, 302, 30);
