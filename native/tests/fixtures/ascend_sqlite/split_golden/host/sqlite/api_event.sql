PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE ApiData(start INTEGER, end INTEGER, connection_id INTEGER, id TEXT);
INSERT INTO ApiData VALUES(1,2,10,'aclrtSynchronizeStream');
COMMIT;
