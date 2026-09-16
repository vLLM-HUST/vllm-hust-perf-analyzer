-- Bind :occurrence_id to one concrete HPO occurrence, not a Position/label.
-- Walk canonical child Occurrences and join retained terminal event identities.
WITH RECURSIVE subtree(occurrence_id) AS (
 SELECT occurrence_id FROM traceloom_v_position_occurrence
 WHERE occurrence_id=:occurrence_id
 UNION
 SELECT m.child_occurrence_id FROM subtree t
 JOIN traceloom_v_position_member m ON m.parent_occurrence_id=t.occurrence_id
 WHERE m.member_kind='child_occurrence'
), terminals AS (
 SELECT DISTINCT m.terminal_anchor_id,m.db_idx,m.device_id FROM subtree t
 JOIN traceloom_v_position_member m ON m.parent_occurrence_id=t.occurrence_id
 WHERE m.member_kind='terminal_token'
), linked AS (
 SELECT DISTINCT d.run_id,d.step_id,d.device_work_id,d.device_id,d.dur_us
 FROM terminals t JOIN traceloom_v_context_anchor a
 ON a.anchor_id=t.terminal_anchor_id AND a.db_idx=t.db_idx AND a.device_id=t.device_id
 JOIN traceloom_v_context_device_work d
 ON d.device_work_id=a.device_work_id AND d.run_id=a.run_id AND d.step_id=a.step_id
)
SELECT s.run_id,s.step_id,s.ordinal,d.device_id,COUNT(*) AS linked_terminal_device_work,
 SUM(d.dur_us) AS linked_terminal_work_us
FROM linked d JOIN traceloom_scheduler_step s USING(run_id,step_id)
GROUP BY s.run_id,s.step_id,d.device_id ORDER BY s.ordinal,d.device_id;
-- Support/aux events outside terminal membership are NOT invented as descendants.
-- Overlapping structural occurrences are not additive partitions of device work.
