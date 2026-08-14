-- Sparse duplicate-observation reconciliation. Filter by event_id,
-- canonical_event_id, envelope_event_id, decision_id, rule_id, or status for
-- a selective audit. Each member says whether it contributes timing, symbol,
-- or cost; normalized events and raw source locators remain independently
-- queryable.
--
-- For effective-policy inspection, outcome summaries, bounded selection, and
-- raw-row drill-down, see docs/event-reconciliation-audit.md.
SELECT decision_id,
       status,
       reason_code,
       event_id,
       member_role,
       contributes_timing,
       contributes_symbol,
       contributes_cost,
       canonical_event_id,
       envelope_event_id,
       canonical_anchor_id,
       observed_symbol,
       canonical_symbol,
       observed_dur_us,
       canonical_anchor_dur_us,
       source_path,
       source_table,
       source_key,
       raw_task_id,
       raw_global_task_id,
       raw_connection_id,
       raw_context_id
FROM traceloom_v_event_reconciliation
ORDER BY db_idx, decision_id, member_order;
