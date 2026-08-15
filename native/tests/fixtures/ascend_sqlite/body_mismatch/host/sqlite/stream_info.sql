PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE CaptureStreamInfo(device_id INTEGER, model_id INTEGER, original_stream_id INTEGER, stream_id INTEGER, timestamp NUMERIC);
INSERT INTO CaptureStreamInfo VALUES(0,7,3,36,1);
COMMIT;
