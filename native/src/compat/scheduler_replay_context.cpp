#include "scheduler_replay_context.h"
#include "sidecar_sqlite_utils.h"

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
namespace traceloom::compat {
void bind_scheduler_replay_context(sqlite3* db) {
  detail::sqlite_exec(db, R"SQL(
CREATE INDEX IF NOT EXISTS idx_context_graph_work
 ON traceloom_device_work(graph_launch_occurrence_id,device_id);
CREATE INDEX IF NOT EXISTS idx_context_event_work
 ON traceloom_device_work(event_id,device_id);
CREATE TABLE traceloom_context_replay_launch_candidate AS
 SELECT c.*,g.launch_id,g.db_idx,g.replay_unit_id,g.anchor_id,
        g.graph_launch_occurrence_id
 FROM traceloom_v_context_direct_device_candidate c
 JOIN traceloom_device_work w USING(device_work_id)
 JOIN traceloom_graph_launch g
 ON g.graph_launch_occurrence_id=w.graph_launch_occurrence_id
 AND g.device_id=w.device_id AND g.db_idx=w.db_idx
 WHERE w.work_kind='graph_launch';
CREATE INDEX idx_context_replay_launch
 ON traceloom_context_replay_launch_candidate(launch_id,db_idx,device_id);
CREATE VIEW traceloom_v_context_replay_member_candidate AS
 SELECT c.run_id,c.step_id,c.execution_id,c.worker_rank,c.phase,c.runtime_call_id,
 d.device_work_id,d.device_id,d.event_id,d.start_ns,d.end_ns,d.dur_us,d.symbol,
 c.provider_support_state,
 c.association_basis||'_then_exact_replay_member' AS association_basis,
 m.launch_id,m.member_id,m.db_idx,m.lane_ordinal,m.task_ordinal
 FROM traceloom_context_replay_launch_candidate c
 JOIN traceloom_graph_body_member m
 ON m.launch_id=c.launch_id AND m.db_idx=c.db_idx AND m.device_id=c.device_id
 JOIN traceloom_device_work d
 ON d.event_id=m.event_id AND d.device_id=m.device_id AND d.db_idx=m.db_idx;
CREATE VIEW traceloom_v_context_device_candidate AS
 SELECT * FROM traceloom_v_context_direct_device_candidate
 UNION ALL
 SELECT run_id,step_id,execution_id,worker_rank,phase,runtime_call_id,
 device_work_id,device_id,event_id,start_ns,end_ns,dur_us,symbol,
 provider_support_state,association_basis
 FROM traceloom_v_context_replay_member_candidate;
CREATE VIEW traceloom_v_context_replay_launch AS
 SELECT c.* FROM traceloom_context_replay_launch_candidate c
 JOIN traceloom_context_device_assignment a USING(device_work_id)
 WHERE a.step_count=1;
CREATE VIEW traceloom_v_context_replay_member AS
 SELECT c.* FROM traceloom_v_context_replay_member_candidate c
 JOIN traceloom_context_device_assignment a USING(device_work_id)
 WHERE a.step_count=1;
CREATE VIEW traceloom_v_context_anchor_candidate AS
 SELECT a.anchor_id,a.db_idx,a.device_id,c.run_id,c.step_id,c.device_work_id
 FROM traceloom_anchor a JOIN traceloom_v_context_device_work c
 ON c.event_id=a.event_id AND c.device_id=a.device_id
 UNION
 SELECT c.anchor_id,c.db_idx,c.device_id,c.run_id,c.step_id,c.device_work_id
 FROM traceloom_v_context_replay_launch c WHERE c.anchor_id IS NOT NULL;
CREATE VIEW traceloom_v_context_anchor AS
 SELECT c.* FROM traceloom_v_context_anchor_candidate c
 JOIN (SELECT anchor_id,db_idx,device_id FROM traceloom_v_context_anchor_candidate
       GROUP BY anchor_id,db_idx,device_id
       HAVING COUNT(DISTINCT json_array(run_id,step_id))=1) a
 USING(anchor_id,db_idx,device_id);
)SQL", "scheduler replay context binding failed");
}
}  // namespace traceloom::compat
#endif
