PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE TaskInfo(
  model_id INT,
  op_name TEXT,
  stream_id INT,
  task_id INT,
  task_type TEXT,
  op_type TEXT,
  index_id,
  device_id INT,
  context_id INT
);
INSERT INTO TaskInfo VALUES(7,'Add',36,11,'KERNEL_AIVEC','Add',-1,0,0);
INSERT INTO TaskInfo VALUES(8,'Sub',37,12,'KERNEL_AIVEC','Sub',-1,0,0);
INSERT INTO TaskInfo VALUES(7,'Add',36,13,'KERNEL_AIVEC','Add',-1,0,0);
COMMIT;
