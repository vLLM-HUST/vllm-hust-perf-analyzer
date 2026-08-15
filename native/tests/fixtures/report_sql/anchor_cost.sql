PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE traceloom_anchor_cost_breakdown (anchor_idx INTEGER NOT NULL, symbol TEXT NOT NULL, anchor_kind TEXT NOT NULL, total_us REAL NOT NULL, self_us REAL NOT NULL, aux_us REAL NOT NULL, graph_child_us REAL NOT NULL, residual_us REAL NOT NULL, raw_child_task_count INTEGER NOT NULL, top_ops TEXT NOT NULL, diagnostic_flags TEXT NOT NULL);
INSERT INTO traceloom_anchor_cost_breakdown VALUES(2,'ACLL','graph_l',123.45600000000000307,1.0,2.0,120.0,0.45600000000000001643,20,'MatMul:16','partial_overlap');
INSERT INTO traceloom_anchor_cost_breakdown VALUES(3,'Kernel','exec',7.0,7.0,0.0,0.0,0.0,0,'','');
COMMIT;
