-- Structural-symbol normalization lineage in recovered structure.
--
-- observed_symbol is the concrete provider/backend label;
-- structural_symbol is the explicit comparison key used by pattern discovery.
-- Equal structural symbols do not establish correspondence by themselves;
-- node_id + occurrence_idx + anchor_order carry recovered correspondence.
-- Filter by anchor_id, node_id, rule_id, structural_symbol, or outcome for a
-- selective audit. Use traceloom_v_symbol_variant_cost to aggregate concrete
-- lowering costs at one recovered position.
SELECT node_id,
       local_node_id,
       occurrence_idx,
       anchor_order,
       anchor_id,
       observed_symbol,
       observed_symbol_source,
       structural_symbol,
       rule_id,
       outcome,
       reason_code,
       anchor_dur_us,
       source_table,
       source_key
FROM traceloom_v_symbol_normalization_placement
WHERE coverage_kind = 'self'
ORDER BY db_idx,
         device_id,
         node_id,
         occurrence_idx,
         anchor_order;
