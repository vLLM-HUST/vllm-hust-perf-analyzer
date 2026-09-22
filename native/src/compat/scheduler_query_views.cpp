#include "scheduler_query_views.h"
#include "sidecar_sqlite_utils.h"

#if defined(TRACELOOM_NATIVE_HAS_SQLITE_COMPAT)
namespace traceloom::compat {
void materialize_scheduler_query_views(sqlite3* db) {
  detail::sqlite_exec(db, R"SQL(
CREATE INDEX idx_scheduler_request_lookup
 ON traceloom_scheduler_request(run_id,request_id,step_id);
CREATE VIEW traceloom_v_scheduler_request_phase AS
 WITH offsets AS (
 SELECT q.*,CASE WHEN json_type(request_json,'$.scheduled_token_start')='integer'
 AND typeof(json_extract(request_json,'$.scheduled_token_start'))='integer'
 AND json_extract(request_json,'$.scheduled_token_start')>=0
 AND json_type(request_json,'$.prompt_tokens')='integer'
 AND typeof(json_extract(request_json,'$.prompt_tokens'))='integer'
 AND json_extract(request_json,'$.prompt_tokens')>=0
 AND json_extract(request_json,'$.scheduled_token_start_basis')='scheduler_output_num_computed_tokens'
 THEN MIN(scheduled_tokens,MAX(0,json_extract(request_json,'$.prompt_tokens')
 -json_extract(request_json,'$.scheduled_token_start'))) END AS scheduled_prompt_tokens
 FROM traceloom_scheduler_request q
 ) SELECT *,scheduled_tokens-scheduled_prompt_tokens AS scheduled_generation_tokens,
 CASE WHEN scheduled_prompt_tokens IS NULL THEN 'unknown'
 WHEN scheduled_tokens=0 THEN 'no_tokens'
 WHEN scheduled_prompt_tokens=scheduled_tokens THEN 'prefill'
 WHEN scheduled_prompt_tokens=0 THEN 'decode' ELSE 'mixed' END AS scheduled_phase
 FROM offsets;
CREATE VIEW traceloom_v_request_step AS
 SELECT s.run_id,s.scheduler_id,q.request_id,s.step_id,s.ordinal,
 q.scheduled_tokens,q.scheduled_prompt_tokens,q.scheduled_generation_tokens,
 q.scheduled_phase,json_extract(q.request_json,'$.payload_kind') AS payload_kind,
 json_extract(q.request_json,'$.prompt_tokens') AS prompt_tokens,
 json_extract(q.request_json,'$.computed_tokens_after_schedule') AS computed_tokens_after_schedule,
 s.total_scheduled_tokens,s.scheduled_requests,
 s.free_blocks_after_schedule,s.pool_blocks_after_schedule,
 src.capture_state,q.source_id,q.line_no
 FROM traceloom_v_scheduler_request_phase q
 JOIN traceloom_scheduler_step s USING(run_id,step_id)
 JOIN traceloom_context_source src ON src.source_id=q.source_id;

-- Lifecycle notifications may arrive on a later scheduling decision, or refer
-- to a request with no participation inside the recorded window. Preserve both.
CREATE TABLE traceloom_request_observation AS
 SELECT s.run_id,s.scheduler_id,s.step_id,s.ordinal,q.request_id,
 'scheduled' AS observation_kind,q.source_id,q.line_no
 FROM traceloom_scheduler_request q JOIN traceloom_scheduler_step s USING(run_id,step_id)
 UNION ALL
 SELECT s.run_id,s.scheduler_id,s.step_id,s.ordinal,j.value AS request_id,
 fields.kind AS observation_kind,r.source_id,r.line_no
 FROM traceloom_scheduler_step s JOIN traceloom_context_record r
 ON r.source_id=s.source_id AND r.line_no=s.line_no
 CROSS JOIN (SELECT 'finished_observed' AS kind,'$.finished_request_ids' AS path
 UNION ALL SELECT 'preempted_observed','$.preempted_request_ids'
 UNION ALL SELECT 'resumed_observed','$.resumed_request_ids') fields
 JOIN json_each(r.raw_json,fields.path) j ON j.type='text';
CREATE INDEX idx_request_observation_lookup ON traceloom_request_observation
 (run_id,scheduler_id,request_id,ordinal);
CREATE VIEW traceloom_v_request_catalog AS
 SELECT o.run_id,o.scheduler_id,o.request_id,
 SUM(o.observation_kind='scheduled') AS participating_steps,
 MIN(CASE WHEN o.observation_kind='scheduled' THEN o.ordinal END) AS first_scheduled_ordinal,
 MAX(CASE WHEN o.observation_kind='scheduled' THEN o.ordinal END) AS last_scheduled_ordinal,
 SUM(o.observation_kind='finished_observed') AS finished_observations,
 SUM(o.observation_kind='preempted_observed') AS preempted_observations,
 SUM(o.observation_kind='resumed_observed') AS resumed_observations,
 'scheduler_observations_not_complete_lifecycle' AS observation_semantics
 FROM traceloom_request_observation o GROUP BY o.run_id,o.scheduler_id,o.request_id;

CREATE VIEW traceloom_v_scheduler_step_shape AS
 WITH phases AS (
 SELECT run_id,step_id,count(scheduled_prompt_tokens) AS classified_requests,
 SUM(scheduled_prompt_tokens) AS prompt_tokens,SUM(scheduled_generation_tokens) AS generation_tokens
 FROM traceloom_v_scheduler_request_phase GROUP BY run_id,step_id
 ) SELECT s.*,
 CASE WHEN s.scheduled_requests=0 AND s.total_scheduled_tokens=0 THEN 'empty_decision'
 WHEN COALESCE(p.classified_requests,0)<>s.scheduled_requests THEN 'unknown'
 WHEN p.prompt_tokens>0 AND p.generation_tokens=0 THEN 'prefill'
 WHEN p.prompt_tokens=0 AND p.generation_tokens>0 THEN 'decode'
 WHEN p.prompt_tokens>0 AND p.generation_tokens>0 THEN 'mixed' ELSE 'unknown' END AS step_kind,
 'scheduled_token_range_relative_to_prompt_not_completion' AS step_kind_basis,
 CASE WHEN p.classified_requests=s.scheduled_requests THEN p.prompt_tokens END AS scheduled_prompt_tokens,
 CASE WHEN p.classified_requests=s.scheduled_requests THEN p.generation_tokens END AS scheduled_generation_tokens,
 CASE WHEN scheduled_requests=0 AND total_scheduled_tokens=0 THEN 'empty_decision'
 WHEN scheduled_requests=1 AND total_scheduled_tokens=1 THEN 'single_request_single_token'
 WHEN scheduled_requests=1 THEN 'single_request_other_tokens'
 ELSE 'multiple_requests' END AS step_shape,
 'observed_scheduling_shape_not_semantic_phase' AS classification_basis
 FROM traceloom_v_scheduler_step_context s LEFT JOIN phases p USING(run_id,step_id);

INSERT INTO traceloom_analysis_surface VALUES
 ('request_catalog','traceloom_v_request_catalog','run/scheduler/request',
 'Requests observed through participation or lifecycle notifications; not complete lifecycles',
 'SELECT * FROM traceloom_v_request_catalog LIMIT 20'),
 ('request_steps','traceloom_v_request_step','run/scheduler/request/step',
 'Recorded participation, not exclusive device-cost ownership',
 'SELECT * FROM traceloom_v_request_step LIMIT 20'),
 ('request_observations','traceloom_request_observation','request/scheduler observation',
 'Scheduling and lifecycle notifications at their observed decision, not true transition times',
 'SELECT * FROM traceloom_request_observation LIMIT 20'),
 ('scheduler_step_shapes','traceloom_v_scheduler_step_shape','run/step',
 'Observed token/request shape, not inferred decode/prefill/mixed labels',
 'SELECT * FROM traceloom_v_scheduler_step_shape LIMIT 20');
INSERT INTO traceloom_projection_recipe VALUES
 ('requests',20,'request','catalog','request','runtime_context','none','(none)',
 'Discover producer-scoped request identities, including notification-only requests',
 'SELECT * FROM traceloom_v_request_catalog ORDER BY run_id,scheduler_id,first_scheduled_ordinal,request_id'),
 ('request_steps',21,'request','selected','step','runtime_context','none',':run_id,:scheduler_id,:request_id',
 'Select the steps a request participated in; shared step costs are not request-owned',
 'SELECT * FROM traceloom_v_request_step WHERE run_id=:run_id AND scheduler_id=:scheduler_id AND request_id=:request_id ORDER BY ordinal'),
 ('request_observations',22,'request','selected','observation','runtime_context','none',':run_id,:scheduler_id,:request_id',
 'Retain scheduling and delayed lifecycle notifications without inferring true transition times',
 'SELECT * FROM traceloom_request_observation WHERE run_id=:run_id AND scheduler_id=:scheduler_id AND request_id=:request_id ORDER BY ordinal,observation_kind'),
 ('scheduler_step_shapes',23,'scheduler_step','selected_shape','step','runtime_context','none',':run_id,:step_shape (NULL selects all)',
 'Select observed step shapes before statistics; no semantic phase classification',
 'SELECT * FROM traceloom_v_scheduler_step_shape WHERE run_id=:run_id AND (:step_shape IS NULL OR step_shape=:step_shape) ORDER BY scheduler_id,ordinal');
INSERT INTO traceloom_projection_recipe VALUES
 ('scheduler_steps_by_kind',24,'scheduler_step','selected_kind','step','runtime_context','none',':run_id,:step_kind (NULL selects all)',
 'Select planned prompt/generation token intervals from worker-input offsets; unknown remains explicit',
 'SELECT * FROM traceloom_v_scheduler_step_shape WHERE run_id=:run_id AND (:step_kind IS NULL OR step_kind=:step_kind) ORDER BY scheduler_id,ordinal');
INSERT INTO traceloom_projection_parameter VALUES
 ('scheduler_steps_by_kind',0,'run_id','TEXT',0,'runtime_run','traceloom_scheduler_step','run_id','Selected run'),
 ('scheduler_steps_by_kind',1,'step_kind','TEXT',1,'scheduler_step_kind','traceloom_v_scheduler_step_shape','step_kind','NULL selects all including unknown');
INSERT INTO traceloom_projection_coordinate VALUES
 ('scheduler_steps_by_kind',0,'run_id','runtime_run','Selected run'),
 ('scheduler_steps_by_kind',1,'step_id','scheduler_step','Supplied step available for device query');
INSERT INTO traceloom_projection_parameter VALUES
 ('request_steps',0,'run_id','TEXT',0,'runtime_run','traceloom_v_request_catalog','run_id','Selected run'),
 ('request_steps',1,'scheduler_id','TEXT',0,'scheduler_producer','traceloom_v_request_catalog','scheduler_id','Pseudonym identity scope'),
 ('request_steps',2,'request_id','TEXT',0,'scheduler_request','traceloom_v_request_catalog','request_id','Producer-local request pseudonym'),
 ('request_observations',0,'run_id','TEXT',0,'runtime_run','traceloom_v_request_catalog','run_id','Selected run'),
 ('request_observations',1,'scheduler_id','TEXT',0,'scheduler_producer','traceloom_v_request_catalog','scheduler_id','Pseudonym identity scope'),
 ('request_observations',2,'request_id','TEXT',0,'scheduler_request','traceloom_v_request_catalog','request_id','Producer-local request pseudonym'),
 ('scheduler_step_shapes',0,'run_id','TEXT',0,'runtime_run','traceloom_scheduler_step','run_id','Selected run'),
 ('scheduler_step_shapes',1,'step_shape','TEXT',1,'scheduler_step_shape','traceloom_v_scheduler_step_shape','step_shape','NULL selects all observed shapes');
INSERT INTO traceloom_projection_coordinate VALUES
 ('requests',0,'run_id','runtime_run','Selected run'),
 ('requests',1,'scheduler_id','scheduler_producer','Identity scope'),
 ('requests',2,'request_id','scheduler_request','Selected request'),
 ('request_steps',0,'run_id','runtime_run','Selected run'),
 ('request_steps',1,'scheduler_id','scheduler_producer','Identity scope'),
 ('request_steps',2,'request_id','scheduler_request','Selected request'),
 ('request_steps',3,'step_id','scheduler_step','Supplied step available for device query'),
 ('request_observations',0,'run_id','runtime_run','Selected run'),
 ('request_observations',1,'scheduler_id','scheduler_producer','Identity scope'),
 ('request_observations',2,'request_id','scheduler_request','Selected request'),
 ('request_observations',3,'step_id','scheduler_step','Decision where notification was observed'),
 ('scheduler_step_shapes',0,'run_id','runtime_run','Selected run'),
 ('scheduler_step_shapes',1,'step_id','scheduler_step','Supplied step available for device query');
)SQL", "failed to materialize scheduler query views");
}
}  // namespace traceloom::compat
#endif
