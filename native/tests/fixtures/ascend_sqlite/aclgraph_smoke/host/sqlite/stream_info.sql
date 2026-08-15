BEGIN;
CREATE TABLE CaptureStreamInfo(
  device_id INTEGER,
  model_id INTEGER,
  original_stream_id INTEGER,
  model_stream_id INTEGER
);
INSERT INTO CaptureStreamInfo VALUES
  (0, 7, 3, 36),
  (0, 8, 3, 37);
COMMIT;
