-- Execute-stream work after completion, plus unrelated concurrent stream work.
INSERT INTO TASK VALUES(125,135,0,99001,100,1,30,0,3,99001,-1);
INSERT INTO TASK VALUES(116,117,0,99002,100,1,30,0,99,99002,-1);
-- The same ordinary operation belongs to three repeated outer sequences,
-- but not to any of the graph bodies composing those sequences.
INSERT INTO TASK VALUES(425,435,0,99003,100,1,30,0,3,99003,-1);
INSERT INTO TASK VALUES(725,735,0,99004,100,1,30,0,3,99004,-1);
INSERT INTO TASK VALUES(1025,1035,0,99005,100,1,30,0,3,99005,-1);
