-- Structure-conditioned device-bubble statistics.
--
-- A bubble is overlap-safe uncovered device time charged immediately before a
-- self-owned anchor occurrence. API observations come from the host interval
-- delimited by supported runtime endpoints of the adjacent device anchors.
-- They are contextual distributions, not a causal explanation of the bubble.


-- 1. Rank recurrent structural positions by bubble cost and show the public
-- typed host-observation support and any compatible API-family distribution
-- observed upstream. A NULL API family retains an unsupported-only or
-- supported-but-empty structural position.
SELECT structural_position_id,
       right_node_symbol,
       bubble_occurrence_count,
       supported_host_occurrence_count,
       missing_endpoint_occurrence_count,
       nonmonotonic_occurrence_count,
       other_unsupported_occurrence_count,
       host_observation_coverage,
       round(total_bubble_us, 3) AS total_bubble_us,
       round(avg_bubble_us, 3) AS avg_bubble_us,
       api_family,
       presence_count,
       presence_fraction_of_all_bubbles,
       presence_fraction_of_observable_bubbles,
       avg_calls_per_bubble,
       avg_calls_per_observable_bubble,
       round(avg_scheduled_overlap_us_per_bubble, 3)
         AS avg_scheduled_overlap_us_per_bubble
FROM traceloom_v_structure_bubble_host_context
ORDER BY total_bubble_us DESC,
         structural_position_id,
         api_family
LIMIT 200;

-- Next, copy a bubble_id from traceloom_v_structure_bubble_occurrence and run
-- docs/report-sql/structure-bubble-drilldown.sql for occurrence details and
-- exact profiler-visible runtime calls.
