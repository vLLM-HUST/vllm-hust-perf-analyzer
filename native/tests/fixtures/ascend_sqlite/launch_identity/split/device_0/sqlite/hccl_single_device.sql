PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE HCCLTaskSingleDevice(
  stream_id INT,
  task_id INT,
  context_id INT,
  op_name TEXT,
  hccl_name TEXT
);
INSERT INTO HCCLTaskSingleDevice VALUES(38,14,0,'hcom_allReduce__fixture','Reduce_Inline');
INSERT INTO HCCLTaskSingleDevice VALUES(38,15,0,'hcom_allReduce__fixture','Reduce_Inline');
COMMIT;
