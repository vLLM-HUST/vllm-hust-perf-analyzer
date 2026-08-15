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
INSERT INTO CaptureStreamInfo VALUES(0,7,3,36,0,0,1);
INSERT INTO CaptureStreamInfo VALUES(0,7,4,37,0,0,1);
INSERT INTO CaptureStreamInfo VALUES(0,8,3,38,0,0,2);
INSERT INTO CaptureStreamInfo VALUES(0,8,4,39,0,0,2);
INSERT INTO CaptureStreamInfo VALUES(0,9,3,40,0,0,3);
INSERT INTO CaptureStreamInfo VALUES(0,9,4,41,0,0,3);
INSERT INTO CaptureStreamInfo VALUES(0,10,3,42,0,0,4);
INSERT INTO CaptureStreamInfo VALUES(0,10,4,43,0,0,4);
INSERT INTO CaptureStreamInfo VALUES(0,11,3,44,0,0,5);
INSERT INTO CaptureStreamInfo VALUES(0,11,4,45,0,0,5);
INSERT INTO CaptureStreamInfo VALUES(0,12,3,46,0,0,6);
INSERT INTO CaptureStreamInfo VALUES(0,12,4,47,0,0,6);
COMMIT;
