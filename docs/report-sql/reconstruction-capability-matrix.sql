with metadata as (
  select
    max(case when key = 'source_kind' then value end) as source_kind,
    max(case when key = 'source_path' then value end) as source_path
  from traceloom_metadata
  having count(*) > 0
), regions as (
  select
    count(*) as region_count,
    sum(case when status = 'recognized_complete_pattern'
             then 1 else 0 end) as recognized_region_count,
    sum(case when status like 'unrecognized_%'
             then 1 else 0 end) as unrecognized_region_count,
    sum(case when status = 'unrecognized_missing_body_capability'
             then 1 else 0 end) as missing_body_capability_count,
    sum(case when status = 'unrecognized_missing_body_evidence'
             then 1 else 0 end) as missing_body_evidence_count,
    sum(case when status = 'unrecognized_missing_completion_evidence'
             then 1 else 0 end) as missing_completion_evidence_count,
    sum(case when status = 'unrecognized_incomplete_tail'
             then 1 else 0 end) as incomplete_tail_count,
    sum(case when status = 'unrecognized_body_mismatch'
             then 1 else 0 end) as body_mismatch_count,
    sum(case when status = 'unrecognized_leading_context'
             then 1 else 0 end) as leading_context_count,
    sum(case when order_policy = 'host_submission_order'
             then 1 else 0 end) as host_order_count,
    sum(case when order_policy = 'device_execution_order'
             then 1 else 0 end) as device_order_count
  from traceloom_aclgraph_reconstruction_region
), replay_modes as (
  select
    graph_provider,
    case when json_valid(raw_json)
      then json_extract(raw_json, '$.reconstruction')
      else ''
    end as reconstruction
  from traceloom_cuda_graph_replay
), replays as (
  select
    sum(case
          when graph_provider = 'aclgraph' and
               reconstruction = 'exact_replay_composition'
            then 1 else 0
        end) as exact_replay_unit_count,
    sum(case
          when graph_provider = 'aclgraph' and
               reconstruction != 'exact_replay_composition'
            then 1 else 0
        end) as legacy_replay_unit_count
  from replay_modes
)
select
  m.source_kind,
  m.source_path,
  case
    when r.region_count = 0 and
         coalesce(p.exact_replay_unit_count, 0) = 0 and
         coalesce(p.legacy_replay_unit_count, 0) = 0
      then 'not_observed'
    when r.missing_body_capability_count > 0
      then 'capability_incomplete'
    when r.unrecognized_region_count > 0
      then 'evidence_incomplete'
    when coalesce(p.exact_replay_unit_count, 0) > 0
      then 'capability_complete'
    when coalesce(p.legacy_replay_unit_count, 0) > 0
      then 'legacy_only'
    else 'observed_unpromoted'
  end as capability_state,
  case
    when r.region_count = 0 then 'not_observed'
    when r.missing_body_capability_count > 0 then 'unavailable'
    when r.missing_body_evidence_count > 0 or r.body_mismatch_count > 0
      then 'contradicted_or_incomplete'
    else 'available'
  end as body_capability,
  case
    when r.region_count = 0 then 'not_observed'
    when r.missing_completion_evidence_count > 0 then 'incomplete'
    else 'available'
  end as completion_capability,
  case
    when r.region_count = 0 then 'not_observed'
    when r.host_order_count = r.region_count then 'host_submission_order'
    when r.device_order_count = r.region_count then 'device_execution_order'
    else 'mixed'
  end as ordering_mode,
  r.region_count,
  r.recognized_region_count,
  r.unrecognized_region_count,
  r.missing_body_capability_count,
  r.missing_body_evidence_count,
  r.missing_completion_evidence_count,
  r.incomplete_tail_count,
  r.body_mismatch_count,
  r.leading_context_count,
  coalesce(p.exact_replay_unit_count, 0) as exact_replay_unit_count,
  coalesce(p.legacy_replay_unit_count, 0) as legacy_replay_unit_count
from metadata m
cross join regions r
cross join replays p;
