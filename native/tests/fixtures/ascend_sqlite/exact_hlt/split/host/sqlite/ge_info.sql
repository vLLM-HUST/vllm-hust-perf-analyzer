PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE TaskInfo(
  model_id INT,
  op_name TEXT,
  stream_id INT,
  task_id INT,
  task_type TEXT,
  op_type TEXT,
  index_id,
  device_id INT,
  context_id INT
);
INSERT INTO TaskInfo VALUES(10,'PrefillHeadOp',42,3,'KERNEL_AIVEC','PrefillHeadOp',-1,0,0);
INSERT INTO TaskInfo VALUES(10,'AuxGraphOp',43,4,'KERNEL_AIVEC','AuxGraphOp',-1,0,0);
INSERT INTO TaskInfo VALUES(11,'PrefillLayerOp',44,8,'KERNEL_AIVEC','PrefillLayerOp',-1,0,0);
INSERT INTO TaskInfo VALUES(11,'AuxGraphOp',45,9,'KERNEL_AIVEC','AuxGraphOp',-1,0,0);
INSERT INTO TaskInfo VALUES(12,'PrefillTailOp',46,13,'KERNEL_AIVEC','PrefillTailOp',-1,0,0);
INSERT INTO TaskInfo VALUES(12,'AuxGraphOp',47,14,'KERNEL_AIVEC','AuxGraphOp',-1,0,0);
INSERT INTO TaskInfo VALUES(7,'HeadOp',36,18,'KERNEL_AIVEC','HeadOp',-1,0,0);
INSERT INTO TaskInfo VALUES(7,'AuxGraphOp',37,19,'KERNEL_AIVEC','AuxGraphOp',-1,0,0);
INSERT INTO TaskInfo VALUES(8,'LayerOp',38,23,'KERNEL_AIVEC','LayerOp',-1,0,0);
INSERT INTO TaskInfo VALUES(8,'AuxGraphOp',39,24,'KERNEL_AIVEC','AuxGraphOp',-1,0,0);
INSERT INTO TaskInfo VALUES(9,'TailOp',40,28,'KERNEL_AIVEC','TailOp',-1,0,0);
INSERT INTO TaskInfo VALUES(9,'AuxGraphOp',41,29,'KERNEL_AIVEC','AuxGraphOp',-1,0,0);
INSERT INTO TaskInfo VALUES(7,'HeadOp',36,33,'KERNEL_AIVEC','HeadOp',-1,0,0);
INSERT INTO TaskInfo VALUES(7,'AuxGraphOp',37,34,'KERNEL_AIVEC','AuxGraphOp',-1,0,0);
INSERT INTO TaskInfo VALUES(8,'LayerOp',38,38,'KERNEL_AIVEC','LayerOp',-1,0,0);
INSERT INTO TaskInfo VALUES(8,'AuxGraphOp',39,39,'KERNEL_AIVEC','AuxGraphOp',-1,0,0);
INSERT INTO TaskInfo VALUES(9,'TailOp',40,43,'KERNEL_AIVEC','TailOp',-1,0,0);
INSERT INTO TaskInfo VALUES(9,'AuxGraphOp',41,44,'KERNEL_AIVEC','AuxGraphOp',-1,0,0);
INSERT INTO TaskInfo VALUES(7,'HeadOp',36,48,'KERNEL_AIVEC','HeadOp',-1,0,0);
INSERT INTO TaskInfo VALUES(7,'AuxGraphOp',37,49,'KERNEL_AIVEC','AuxGraphOp',-1,0,0);
INSERT INTO TaskInfo VALUES(8,'LayerOp',38,53,'KERNEL_AIVEC','LayerOp',-1,0,0);
INSERT INTO TaskInfo VALUES(8,'AuxGraphOp',39,54,'KERNEL_AIVEC','AuxGraphOp',-1,0,0);
INSERT INTO TaskInfo VALUES(9,'TailOp',40,58,'KERNEL_AIVEC','TailOp',-1,0,0);
INSERT INTO TaskInfo VALUES(9,'AuxGraphOp',41,59,'KERNEL_AIVEC','AuxGraphOp',-1,0,0);
INSERT INTO TaskInfo VALUES(7,'HeadOp',36,63,'KERNEL_AIVEC','HeadOp',-1,0,0);
INSERT INTO TaskInfo VALUES(7,'AuxGraphOp',37,64,'KERNEL_AIVEC','AuxGraphOp',-1,0,0);
INSERT INTO TaskInfo VALUES(8,'LayerOp',38,68,'KERNEL_AIVEC','LayerOp',-1,0,0);
INSERT INTO TaskInfo VALUES(8,'AuxGraphOp',39,69,'KERNEL_AIVEC','AuxGraphOp',-1,0,0);
COMMIT;
