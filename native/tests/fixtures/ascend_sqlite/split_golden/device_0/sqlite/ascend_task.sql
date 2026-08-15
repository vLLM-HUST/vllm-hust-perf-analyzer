PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE AscendTask(model_id INTEGER, index_id INTEGER, stream_id INTEGER, task_id INTEGER, context_id INTEGER, batch_id INTEGER, start_time NUMERIC, duration NUMERIC, host_task_type TEXT, device_task_type TEXT, connection_id INTEGER);
INSERT INTO AscendTask VALUES(2,-1,3,99,0,0,100,60,'AI_CORE','AI_CORE',700);
INSERT INTO AscendTask VALUES(2,-1,3,100,0,0,170,40,'EVENT_WAIT','UNKNOWN',701);
INSERT INTO AscendTask VALUES(2,-1,4,101,0,0,220,40,'FFTS_PLUS','SDMA',702);
INSERT INTO AscendTask VALUES(2,-1,3,101,0,0,-1,-1,'AI_CORE','UNKNOWN',702);
COMMIT;
