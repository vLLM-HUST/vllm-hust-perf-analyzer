BEGIN;
ALTER TABLE TaskInfo RENAME TO CompleteTaskInfo;
CREATE TABLE TaskInfo AS
  SELECT device_id, stream_id, task_id, context_id, op_name, task_type
  FROM CompleteTaskInfo;
COMMIT;
