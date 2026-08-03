-- Idle evidence golden fixture: "host wait exists, but visible idle is zero".
--
-- Counterexample to the naive equivalence "host wait implies device visible
-- idle": the host-side sync API event (aclrtSynchronizeStream) exists in the
-- capture, yet the device productive timeline covers the whole analysis span
-- back-to-back, so the analyzer MUST report zero visible_productive_idle.
--
-- Rebuild the database from this file with:
--   sqlite3 host_wait_zero_visible_idle.db < fixture.sql
-- (the checked-in .db is the golden artifact the test loads; keep it in sync
-- with this SQL).

CREATE TABLE STRING_IDS(id INTEGER PRIMARY KEY, value TEXT);
INSERT INTO STRING_IDS(id, value) VALUES
  (10, 'AI_CORE'),
  (11, 'MIX_AIC'),
  (20, 'matmul_ffn'),        -- op name blob containing "matmul"
  (21, 'MatMul'),            -- op type
  (30, 'aclrtSynchronizeStream');  -- host sync API (host wait)

CREATE TABLE TASK(
  startNs INTEGER, endNs INTEGER, deviceId INTEGER, connectionId INTEGER,
  globalTaskId INTEGER, globalPid INTEGER, taskType INTEGER, contextId INTEGER,
  streamId INTEGER, taskId INTEGER, modelId INTEGER);
-- Back-to-back productive compute tasks: [1000,4000) with zero gaps.
INSERT INTO TASK(startNs, endNs, deviceId, connectionId, globalTaskId,
                 globalPid, taskType, contextId, streamId, taskId, modelId)
VALUES
  (1000, 2000, 0, 700, 9001, 1, 10, 0, 3, 101, 2),
  (2000, 3000, 0, 701, 9002, 1, 10, 0, 3, 102, 2),
  (3000, 4000, 0, 702, 9003, 1, 10, 0, 3, 103, 2);

CREATE TABLE COMPUTE_TASK_INFO(globalTaskId INTEGER, name INTEGER,
                               opType INTEGER, taskType INTEGER);
INSERT INTO COMPUTE_TASK_INFO(globalTaskId, name, opType, taskType) VALUES
  (9001, 20, 21, 11),
  (9002, 20, 21, 11),
  (9003, 20, 21, 11);

-- Host wait: aclrtSynchronizeStream [500,4500) — spans the whole device
-- timeline. This is host-side evidence only; it MUST NOT create device
-- visible idle.
CREATE TABLE CANN_API(
  startNs INTEGER, endNs INTEGER, type INTEGER, globalTid INTEGER,
  connectionId INTEGER, name INTEGER);
INSERT INTO CANN_API(startNs, endNs, type, globalTid, connectionId, name)
VALUES (500, 4500, 0, 1, 9000, 30);
