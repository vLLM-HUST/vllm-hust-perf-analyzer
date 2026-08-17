#!/usr/bin/env python3
"""Verify the coordinate-preserving TraceLoom tour against one analysis DB."""

from __future__ import annotations

import argparse
import sqlite3
from pathlib import Path


EXPECTED_CONTRACT = "scope_population_resolution_domain_lens_coordinates_v2"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def require_close(actual: float, expected: float, message: str) -> None:
    require(abs(actual - expected) <= 0.001, f"{message}: {actual} != {expected}")


def scalar(db: sqlite3.Connection, sql: str, parameters: tuple[object, ...] = ()) -> object:
    row = db.execute(sql, parameters).fetchone()
    require(row is not None, f"query returned no row: {sql}")
    return row[0]


def quote_identifier(identifier: str) -> str:
    return '"' + identifier.replace('"', '""') + '"'


def require_embedded_row(db: sqlite3.Connection, locator: sqlite3.Row) -> None:
    require(locator["resolution_status"] == "embedded_raw", str(dict(locator)))
    table = locator["embedded_table_name"]
    row_column = locator["source_rowid_column"]
    source_key = locator["source_key"]
    require(table and row_column and source_key is not None, str(dict(locator)))
    count = scalar(
        db,
        f"SELECT COUNT(*) FROM {quote_identifier(table)} "
        f"WHERE {quote_identifier(row_column)} = ?",
        (source_key,),
    )
    require(count == 1, f"embedded source row not found: {dict(locator)}")


def verify(database: Path) -> None:
    db = sqlite3.connect(f"file:{database}?mode=ro", uri=True)
    db.row_factory = sqlite3.Row
    try:
        contract = scalar(
            db,
            "SELECT value FROM traceloom_metadata "
            "WHERE key='analytical_projection_contract'",
        )
        require(contract == EXPECTED_CONTRACT, f"unexpected contract: {contract}")

        required_recipes = {
            "scope_catalog",
            "scope_occurrences",
            "scope_hierarchy",
            "scope_members",
            "position_population",
            "position_occurrences",
            "scope_host_windows",
            "scope_host_context",
            "bubble_hotspots",
            "bubble_occurrences",
            "bubble_host_context",
            "host_window_calls",
            "runtime_call_audit",
            "event_audit",
        }
        recipes = {
            row[0] for row in db.execute("SELECT projection_name FROM traceloom_projection_recipe")
        }
        require(required_recipes <= recipes, f"missing recipes: {required_recipes - recipes}")

        required_transitions = {
            ("position_population", "position_occurrences"),
            ("position_occurrences", "event_audit"),
            ("bubble_hotspots", "bubble_occurrences"),
            ("bubble_occurrences", "host_window_calls"),
            ("host_window_calls", "runtime_call_audit"),
        }
        transitions = {
            (row[0], row[1])
            for row in db.execute(
                "SELECT DISTINCT source_projection, target_projection "
                "FROM traceloom_v_projection_continuation"
            )
        }
        require(
            required_transitions <= transitions,
            f"missing continuations: {required_transitions - transitions}",
        )
        forbidden_transitions = {
            ("scope_catalog", "bubble_occurrences"),
            ("scope_catalog", "bubble_host_context"),
        }
        require(
            not (forbidden_transitions & transitions),
            "coordinate typing advertised a node id as a bubble position: "
            f"{forbidden_transitions & transitions}",
        )

        scope = db.execute(
            "SELECT node_id, occurrence_count FROM traceloom_v_tree_node "
            "WHERE view_name='native_report_tree' AND repeat_count=24 "
            "AND occurrence_count>1 ORDER BY occurrence_count DESC, display_order LIMIT 1"
        ).fetchone()
        require(scope is not None, "no repeated kickstart tour scope")
        node_id = scope["node_id"]
        require(
            scalar(
                db,
                "SELECT COUNT(*) FROM traceloom_tree_node_occurrence WHERE node_id=?",
                (node_id,),
            )
            == scope["occurrence_count"],
            "occurrence projection changed scope population",
        )
        require(
            scalar(
                db,
                "SELECT COUNT(*) FROM traceloom_v_node_children WHERE parent_node_id=?",
                (node_id,),
            )
            > 0,
            "hierarchy projection is empty",
        )

        occurrences = list(
            db.execute(
                "SELECT occurrence_idx, anchor_count, total_us, compute_us, "
                "comm_us, idle_us, aux_us FROM traceloom_tree_node_occurrence "
                "WHERE node_id=? ORDER BY total_us, occurrence_idx",
                (node_id,),
            )
        )
        require(len(occurrences) == 29, "kickstart occurrence population changed")
        require(
            {row["anchor_count"] for row in occurrences} == {384},
            "kickstart occurrence extents changed",
        )
        median = occurrences[len(occurrences) // 2]
        outlier = occurrences[-1]
        require(median["occurrence_idx"] == 23, "median occurrence changed")
        require(outlier["occurrence_idx"] == 26, "slowest occurrence changed")
        require_close(median["total_us"], 58489.009, "median total")
        require_close(outlier["total_us"], 68061.180, "outlier total")
        require_close(
            outlier["compute_us"] - median["compute_us"],
            -616.133,
            "outlier compute delta",
        )
        require_close(
            outlier["comm_us"] - median["comm_us"],
            10255.221,
            "outlier communication delta",
        )
        require_close(
            outlier["idle_us"] - median["idle_us"],
            -66.917,
            "outlier uncovered delta",
        )

        position_rows = list(
            db.execute(
                "WITH ranked_positions AS ("
                "SELECT na.*, row_number() OVER (PARTITION BY na.node_id, "
                "na.anchor_order, na.coverage_kind ORDER BY na.total_us, "
                "na.occurrence_idx) AS cost_rank, count(*) OVER (PARTITION BY "
                "na.node_id, na.anchor_order, na.coverage_kind) AS "
                "population_count FROM traceloom_tree_node_anchor na WHERE "
                "na.node_id=?), position_medians AS (SELECT node_id, "
                "anchor_order, coverage_kind, total_us AS median_position_us "
                "FROM ranked_positions WHERE cost_rank=(population_count+1)/2) "
                "SELECT selected.anchor_order, a.symbol, e.event_id, "
                "selected.total_us-median.median_position_us AS excess_us "
                "FROM ranked_positions selected JOIN position_medians median "
                "ON median.node_id=selected.node_id AND median.anchor_order="
                "selected.anchor_order AND median.coverage_kind="
                "selected.coverage_kind JOIN traceloom_anchor a ON "
                "a.anchor_id=selected.anchor_id AND a.db_idx=selected.db_idx "
                "AND a.device_id=selected.device_id LEFT JOIN traceloom_event "
                "e ON e.event_id=a.event_id AND e.db_idx=a.db_idx AND "
                "e.device_id=a.device_id WHERE selected.occurrence_idx=? "
                "ORDER BY excess_us DESC, selected.anchor_order LIMIT 12",
                (node_id, outlier["occurrence_idx"]),
            )
        )
        require(len(position_rows) == 12, "outlier position projection is incomplete")
        require(
            {row["symbol"] for row in position_rows} == {"AllReduce"},
            "largest outlier position deltas changed family",
        )
        member = position_rows[0]
        require(member["anchor_order"] == 282, "largest outlier position changed")
        require(member["event_id"] == "event-86482", "outlier event changed")
        require_close(member["excess_us"], 1916.198, "largest position excess")

        event_locator = db.execute(
            "SELECT * FROM traceloom_v_event_source_locator "
            "WHERE event_id=? ORDER BY source_ordinal LIMIT 1",
            (member["event_id"],),
        ).fetchone()
        require(event_locator is not None, "event locator is empty")
        require(event_locator["source_table"] == "COMMUNICATION_OP", str(dict(event_locator)))
        require(event_locator["source_key"] == "109", str(dict(event_locator)))
        require_embedded_row(db, event_locator)

        host_window_count = scalar(
            db,
            "SELECT COUNT(*) FROM traceloom_v_node_host_interval WHERE node_id=?",
            (node_id,),
        )
        require(host_window_count > 0, "typed host-window projection is empty")
        require(
            scalar(
                db,
                "SELECT COUNT(*) FROM traceloom_v_node_host_interval "
                "WHERE node_id=? AND support_state!='supported_ordered'",
                (node_id,),
            )
            > 0,
            "kickstart scope no longer exercises a typed host boundary",
        )

        position_count = scalar(db, "SELECT COUNT(*) FROM traceloom_v_structure_bubble_position")
        context_position_count = scalar(
            db,
            "SELECT COUNT(DISTINCT structural_position_id) "
            "FROM traceloom_v_structure_bubble_host_context",
        )
        require(
            position_count == context_position_count,
            "changing to host context erased a bubble position",
        )

        bubble = db.execute(
            "SELECT b.* FROM traceloom_v_structure_bubble_occurrence b "
            "WHERE b.host_observation_status='supported_ordered' "
            "AND EXISTS (SELECT 1 FROM traceloom_v_anchor_host_activity h "
            "WHERE h.interval_id=b.host_interval_id) "
            "ORDER BY b.bubble_us DESC, b.bubble_id LIMIT 1"
        ).fetchone()
        require(bubble is not None, "no bubble occurrence with literal host calls")
        runtime_locator = db.execute(
            "SELECT l.* FROM traceloom_v_anchor_host_activity h "
            "JOIN traceloom_v_runtime_call_source_locator l "
            "ON l.runtime_call_id=h.observed_runtime_call_id "
            "WHERE h.interval_id=? ORDER BY h.observed_order LIMIT 1",
            (bubble["host_interval_id"],),
        ).fetchone()
        require(runtime_locator is not None, "runtime-call audit projection is empty")
        require_embedded_row(db, runtime_locator)
    finally:
        db.close()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("database", type=Path)
    args = parser.parse_args()
    verify(args.database.resolve())
    print("projection tour: PASS")


if __name__ == "__main__":
    main()
