PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE HCCLTaskSingleDevice(
  stream_id INT,
  task_id INT,
  context_id INT,
  op_name TEXT,
  hccl_name TEXT
);
COMMIT;
