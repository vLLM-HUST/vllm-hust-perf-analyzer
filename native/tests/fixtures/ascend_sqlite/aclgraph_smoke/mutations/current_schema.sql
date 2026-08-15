BEGIN;
ALTER TABLE CaptureStreamInfo RENAME COLUMN model_stream_id TO stream_id;
ALTER TABLE CaptureStreamInfo ADD COLUMN batch_id INTEGER;
ALTER TABLE CaptureStreamInfo ADD COLUMN capture_status INTEGER;
ALTER TABLE CaptureStreamInfo ADD COLUMN timestamp NUMERIC;
COMMIT;
