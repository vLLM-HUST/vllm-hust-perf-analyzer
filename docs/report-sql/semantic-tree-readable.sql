SELECT line
FROM traceloom_v_semantic_tree_readable
WHERE view_name = 'anchor_tree'
ORDER BY db_idx, device_id, tree_kind, preorder_idx;
