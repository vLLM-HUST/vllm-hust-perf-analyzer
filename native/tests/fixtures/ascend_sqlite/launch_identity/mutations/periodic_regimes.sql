-- Two independently periodic single-graph populations on one device.
-- Previously only the final B B B suffix was published; A A A became unknown.
CREATE TEMP TABLE seeds AS SELECT * FROM TASK WHERE startNs<300;
DELETE FROM TASK;
WITH copies(i) AS (VALUES(0),(1),(2),(3),(4),(5))
INSERT INTO TASK
SELECT startNs + CASE WHEN i<3 THEN i*100 ELSE (i-1)*100 END,
       endNs + CASE WHEN i<3 THEN i*100 ELSE (i-1)*100 END,
       deviceId,
       CASE WHEN taskType IN (10,11) THEN 100+i ELSE connectionId END,
       globalTaskId,globalPid,taskType,contextId,streamId,
       taskId+i*100,modelId
FROM seeds,copies
WHERE (i<3 AND startNs<200) OR (i>=3 AND startNs>=200);
DROP TABLE seeds;
