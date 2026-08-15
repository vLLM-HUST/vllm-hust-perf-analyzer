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
INSERT INTO ApiData VALUES('api','aclmdlRIExecuteAsync','node',1,'1',90,95,1000);
INSERT INTO ApiData VALUES('api','aclmdlRIExecuteAsync','node',1,'2',190,195,1001);
INSERT INTO ApiData VALUES('api','aclmdlRIExecuteAsync','node',1,'3',290,295,1002);
INSERT INTO ApiData VALUES('api','aclmdlRIExecuteAsync','node',1,'4',390,395,1003);
INSERT INTO ApiData VALUES('api','aclmdlRIExecuteAsync','node',1,'5',490,495,1004);
INSERT INTO ApiData VALUES('api','aclmdlRIExecuteAsync','node',1,'6',590,595,1005);
INSERT INTO ApiData VALUES('api','aclmdlRIExecuteAsync','node',1,'7',690,695,1006);
INSERT INTO ApiData VALUES('api','aclmdlRIExecuteAsync','node',1,'8',790,795,1007);
INSERT INTO ApiData VALUES('api','aclmdlRIExecuteAsync','node',1,'9',890,895,1008);
INSERT INTO ApiData VALUES('api','aclmdlRIExecuteAsync','node',1,'10',990,995,1009);
INSERT INTO ApiData VALUES('api','aclmdlRIExecuteAsync','node',1,'11',1090,1095,1010);
INSERT INTO ApiData VALUES('api','aclmdlRIExecuteAsync','node',1,'12',1190,1195,1011);
INSERT INTO ApiData VALUES('api','aclmdlRIExecuteAsync','node',1,'13',1290,1295,1012);
INSERT INTO ApiData VALUES('api','aclmdlRIExecuteAsync','node',1,'14',1390,1395,1013);
INSERT INTO ApiData VALUES('api','aclrtSynchronizeStreamWithTimeout','node',1,'15',1490,1500,9999);
COMMIT;
