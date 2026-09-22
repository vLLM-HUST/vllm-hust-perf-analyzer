-- Preserve the launch/completion chain but remove one earlier exact body.
DELETE FROM TASK WHERE startNs>=300 AND startNs<400 AND taskType=30;
