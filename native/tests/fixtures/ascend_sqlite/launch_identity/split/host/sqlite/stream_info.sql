PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE CaptureStreamInfo(
  device_id INT,
  model_id INT,
  original_stream_id INT,
  stream_id INT,
  batch_id,
  capture_status,
  timestamp NUM
);
INSERT INTO CaptureStreamInfo VALUES(0,7,3,36,0,0,100);
INSERT INTO CaptureStreamInfo VALUES(0,7,4,38,0,0,100);
INSERT INTO CaptureStreamInfo VALUES(0,8,3,37,0,0,200);
COMMIT;
