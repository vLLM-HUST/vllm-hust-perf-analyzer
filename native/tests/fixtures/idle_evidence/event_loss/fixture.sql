-- Golden fixture class: event_loss.
-- An unknown visible event remains diagnostic lineage while incomplete
-- collection prevents every absence promotion in the surrounding gap.

CREATE TABLE STRING_IDS(id INTEGER PRIMARY KEY, value TEXT);
INSERT INTO STRING_IDS(id, value) VALUES
  (10, 'AI_CORE'), (11, 'MIX_AIC'), (12, 'MYSTERY_EVENT'),
  (20, 'matmul_fixture'), (21, 'MatMul'), (22, 'opaque_event');

CREATE TABLE TASK(
  startNs INTEGER, endNs INTEGER, deviceId INTEGER, connectionId INTEGER,
  globalTaskId INTEGER, globalPid INTEGER, taskType INTEGER, contextId INTEGER,
  streamId INTEGER, taskId INTEGER, modelId INTEGER);
INSERT INTO TASK(startNs, endNs, deviceId, connectionId, globalTaskId,
                 globalPid, taskType, contextId, streamId, taskId, modelId)
VALUES
  (1000, 2000, 0, 201, 2001, 1, 10, 0, 3, 1, 1),
  (3000, 4000, 0, 202, 2002, 1, 12, 0, 7, 2, 1),
  (5000, 6000, 0, 203, 2003, 1, 10, 0, 3, 3, 1);

CREATE TABLE COMPUTE_TASK_INFO(globalTaskId INTEGER, name INTEGER,
                               opType INTEGER, taskType INTEGER);
INSERT INTO COMPUTE_TASK_INFO(globalTaskId, name, opType, taskType) VALUES
  (2001, 20, 21, 11),
  (2002, 22, 22, 12),
  (2003, 20, 21, 11);

CREATE TABLE CANN_API(
  startNs INTEGER, endNs INTEGER, type INTEGER, globalTid INTEGER,
  connectionId INTEGER, name INTEGER);
