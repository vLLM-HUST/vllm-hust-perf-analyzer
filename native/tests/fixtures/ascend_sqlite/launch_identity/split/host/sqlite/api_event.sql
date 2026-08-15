PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE ApiData(
  struct_type,
  id TEXT,
  level,
  thread_id INT,
  item_id TEXT,
  start INT,
  "end" INT,
  connection_id INT
);
INSERT INTO ApiData VALUES('api','aclmdlRICaptureBegin','node',1,'1',10,11,10);
INSERT INTO ApiData VALUES('api','aclnnMuls','node',1,'2',12,13,11);
INSERT INTO ApiData VALUES('api','aclnnAdds','node',1,'3',14,15,12);
INSERT INTO ApiData VALUES('api','aclmdlRICaptureEnd','node',1,'4',16,17,13);
INSERT INTO ApiData VALUES('api','aclmdlRICaptureBegin','node',1,'5',20,21,20);
INSERT INTO ApiData VALUES('api','aclnnMuls','node',1,'6',22,23,21);
INSERT INTO ApiData VALUES('api','aclnnSubs','node',1,'7',24,25,22);
INSERT INTO ApiData VALUES('api','aclmdlRICaptureEnd','node',1,'8',26,27,23);
INSERT INTO ApiData VALUES('api','aclmdlRIExecuteAsync','node',1,'9',90,95,100);
INSERT INTO ApiData VALUES('api','aclmdlRIExecuteAsync','node',1,'10',190,195,101);
INSERT INTO ApiData VALUES('api','aclmdlRIExecuteAsync','node',1,'11',290,295,102);
INSERT INTO ApiData VALUES('api','aclmdlRIExecuteAsync','node',1,'12',799990,799995,103);
INSERT INTO ApiData VALUES('api','aclrtSynchronizeStreamWithTimeout','node',1,'13',250,260,200);
INSERT INTO ApiData VALUES('api','aclrtSynchronizeStreamWithTimeout','node',1,'14',800020,800030,201);
COMMIT;
