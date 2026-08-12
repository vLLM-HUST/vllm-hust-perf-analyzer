#!/usr/bin/env python3
"""Render the README product view from a real TraceLoom analysis database."""

from __future__ import annotations

import argparse
import html
import sqlite3
from pathlib import Path


WIDTH = 1600
HEIGHT = 900


def esc(value: object) -> str:
    return html.escape(str(value), quote=True)


def text(x: float, y: float, value: object, css: str = "body") -> str:
    return f'<text x="{x}" y="{y}" class="{css}">{esc(value)}</text>'


def rect(
    x: float,
    y: float,
    width: float,
    height: float,
    css: str,
    radius: float = 12,
) -> str:
    return (
        f'<rect x="{x}" y="{y}" width="{width}" height="{height}" '
        f'rx="{radius}" class="{css}"/>'
    )


def query_one(db: sqlite3.Connection, sql: str) -> sqlite3.Row:
    row = db.execute(sql).fetchone()
    if row is None:
        raise RuntimeError("the analysis DB has no nested repeated structure")
    return row


def render(database: Path, theme: str = "dark") -> str:
    db = sqlite3.connect(f"file:{database}?mode=ro", uri=True)
    db.row_factory = sqlite3.Row
    target = query_one(
        db,
        """
        SELECT node_id, local_node_id, label, path, repeat_count,
               occurrence_count
        FROM traceloom_v_tree_node
        WHERE view_name = 'native_report_tree'
          AND repeat_count = 24
          AND occurrence_count > 1
        ORDER BY occurrence_count DESC, display_order
        LIMIT 1
        """,
    )
    parent = query_one(
        db,
        f"""
        SELECT p.local_node_id, p.label, p.occurrence_count
        FROM traceloom_v_tree_node n
        JOIN traceloom_v_tree_node p ON p.node_id = n.parent_node_id
        WHERE n.node_id = {sql_literal(target['node_id'])}
        """,
    )
    children = db.execute(
        """
        SELECT label, avg_total_us
        FROM traceloom_v_tree_node
        WHERE parent_node_id = ?
        ORDER BY display_order
        """,
        (target["node_id"],),
    ).fetchall()
    occurrences = db.execute(
        """
        SELECT occurrence_idx, total_us
        FROM traceloom_tree_node_occurrence
        WHERE local_node_id = ?
        ORDER BY occurrence_idx
        """,
        (target["local_node_id"],),
    ).fetchall()
    evidence = query_one(
        db,
        f"""
        WITH selected AS (
          SELECT na.local_node_id, na.occurrence_idx, na.anchor_order,
                 a.symbol, e.event_id, e.dur_us
          FROM traceloom_tree_node_anchor na
          JOIN traceloom_anchor a USING(anchor_id)
          JOIN traceloom_event e USING(event_id)
          WHERE na.local_node_id = {sql_literal(target['local_node_id'])}
            AND na.occurrence_idx = 1
          ORDER BY e.dur_us DESC, na.anchor_order
          LIMIT 1
        )
        SELECT s.*, l.source_table, l.source_key, l.resolution_status
        FROM selected s
        JOIN traceloom_v_event_source_locator l USING(event_id)
        """,
    )
    db.close()

    values = [float(row["total_us"]) / 1000.0 for row in occurrences]
    minimum = min(values)
    maximum = max(values)
    mean = sum(values) / len(values)
    child_names = [row["label"] for row in children]

    if theme == "paper":
        colors = {
            "bg": "#ffffff",
            "panel": "#f7f9fc",
            "panel_stroke": "#a9b6c8",
            "terminal": "#f3f6fa",
            "terminal_stroke": "#8ea0b8",
            "chip": "#eef3f9",
            "chip_stroke": "#9aabc1",
            "chip_hot": "#e1f5f1",
            "hot": "#087f72",
            "bar": "#5274c8",
            "bar_hot": "#16a394",
            "guide": "#b2bfce",
            "title": "#172235",
            "section": "#172235",
            "body": "#26364c",
            "muted": "#53657c",
            "label": "#53657c",
            "highlight": "#087f72",
        }
        headline = "Queryable hierarchical cost view"
        subtitle = (
            "Horizontal drill-down preserves evidence; vertical alignment "
            "defines comparable cost populations."
        )
    else:
        colors = {
            "bg": "#09111f",
            "panel": "#101b2d",
            "panel_stroke": "#2b3b55",
            "terminal": "#0b1424",
            "terminal_stroke": "#344765",
            "chip": "#172840",
            "chip_stroke": "#3b5477",
            "chip_hot": "#243c5f",
            "hot": "#62d9c5",
            "bar": "#5d78d8",
            "bar_hot": "#62d9c5",
            "guide": "#354964",
            "title": "#f5f7fb",
            "section": "#f0f4fb",
            "body": "#dce6f4",
            "muted": "#9eb0c9",
            "label": "#a9bad1",
            "highlight": "#75ead5",
        }
        headline = "One artifact. Two analytical directions."
        subtitle = (
            "TraceLoom turns raw profiler rows into a queryable "
            "hierarchical cost map."
        )

    css = """<style>
          .bg {{ fill: {bg}; }}
          .panel {{ fill: {panel}; stroke: {panel_stroke}; stroke-width: 1.5; }}
          .terminal {{ fill: {terminal}; stroke: {terminal_stroke}; stroke-width: 1.5; }}
          .chip {{ fill: {chip}; stroke: {chip_stroke}; stroke-width: 1; }}
          .chip-hot {{ fill: {chip_hot}; stroke: {hot}; stroke-width: 1.5; }}
          .bar {{ fill: {bar}; }}
          .bar-hot {{ fill: {bar_hot}; }}
          .guide {{ stroke: {guide}; stroke-width: 1; }}
          .arrow {{ stroke: {hot}; stroke-width: 2.5; fill: none; }}
          text {{ font-family: Inter, ui-sans-serif, system-ui, sans-serif; }}
          .title {{ fill: {title}; font-size: 38px; font-weight: 700; }}
          .subtitle {{ fill: {muted}; font-size: 19px; }}
          .section {{ fill: {section}; font-size: 23px; font-weight: 650; }}
          .label {{ fill: {label}; font-size: 15px; font-weight: 600; }}
          .body {{ fill: {body}; font-size: 16px; }}
          .small {{ fill: {muted}; font-size: 13px; }}
          .mono {{ fill: {body}; font-size: 15px; font-family: ui-monospace,
                  SFMono-Regular, Menlo, Consolas, monospace; }}
          .mono-hot {{ fill: {highlight}; font-size: 15px; font-weight: 650;
                      font-family: ui-monospace, SFMono-Regular, Menlo,
                      Consolas, monospace; }}
          .metric {{ fill: {highlight}; font-size: 20px; font-weight: 700; }}
        </style>""".format(**colors)

    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" '
        f'height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}" role="img" '
        'aria-labelledby="title description">',
        '<title id="title">TraceLoom augmented database query experience</title>',
        '<desc id="description">A hierarchical execution structure supports '
        'horizontal evidence drill-down and vertical cost comparison across '
        'equivalent occurrences.</desc>',
        css,
        rect(0, 0, WIDTH, HEIGHT, "bg", 0),
        text(64, 72, headline, "title"),
        text(64, 106, subtitle, "subtitle"),
    ]

    # Horizontal structure panel.
    out += [
        rect(48, 140, 1504, 275, "panel", 18),
        text(76, 182, "HORIZONTAL  ·  drill from structure to evidence", "section"),
        text(
            76,
            213,
            "One recovered execution path retains ordered children and raw-row provenance.",
            "subtitle",
        ),
        rect(76, 242, 170, 54, "chip-hot", 10),
        text(94, 275, f"{parent['label']}", "mono-hot"),
        text(264, 275, "›", "metric"),
        rect(294, 242, 170, 54, "chip-hot", 10),
        text(312, 275, f"{target['label']}", "mono-hot"),
    ]
    x = 490
    shown = child_names[:8]
    for name in shown:
        width = max(88, min(154, 30 + len(name) * 8.5))
        out.append(rect(x, 242, width, 54, "chip", 10))
        out.append(text(x + 15, 275, name, "mono"))
        x += width + 10
    if len(child_names) > len(shown):
        out.append(
            text(
                min(x, 1460),
                275,
                f"+{len(child_names) - len(shown)}",
                "small",
            )
        )

    out += [
        rect(76, 322, 1400, 66, "terminal", 9),
        text(96, 349, "sqlite›", "mono-hot"),
        text(168, 349, "node → occurrence #1 →", "mono"),
        text(405, 349, evidence["symbol"], "mono-hot"),
        text(530, 349, "→", "mono"),
        text(558, 349, evidence["event_id"], "mono-hot"),
        text(690, 349, "→", "mono"),
        text(
            718,
            349,
            f"{evidence['source_table']} row {evidence['source_key']}",
            "mono-hot",
        ),
        text(
            96,
            374,
            f"{float(evidence['dur_us']):.3f} µs · "
            f"{evidence['resolution_status']} · raw evidence is inside this DB",
            "small",
        ),
    ]

    # Vertical comparison panel.
    out += [
        rect(48, 439, 1504, 370, "panel", 18),
        text(76, 481, "VERTICAL  ·  compare equivalent occurrences", "section"),
        text(
            76,
            512,
            f"{target['local_node_id']} {target['label']} appears "
            f"{len(values)} times under {parent['label']}; structure fixes "
            "the comparison scope.",
            "subtitle",
        ),
    ]
    chart_x = 86
    chart_y = 562
    chart_w = 1015
    chart_h = 176
    out += [
        (
            f'<line x1="{chart_x}" y1="{chart_y + chart_h}" '
            f'x2="{chart_x + chart_w}" y2="{chart_y + chart_h}" '
            'class="guide"/>'
        ),
        (
            f'<line x1="{chart_x}" y1="{chart_y}" '
            f'x2="{chart_x + chart_w}" y2="{chart_y}" '
            'class="guide"/>'
        ),
    ]
    slot = chart_w / len(values)
    span = maximum - minimum or 1.0
    for index, value in enumerate(values):
        normalized = 0.25 + 0.75 * (value - minimum) / span
        height = normalized * chart_h
        x_pos = chart_x + index * slot + 3
        css = "bar-hot" if value in (minimum, maximum) else "bar"
        out.append(
            rect(
                x_pos,
                chart_y + chart_h - height,
                max(5, slot - 7),
                height,
                css,
                3,
            )
        )
        if (index + 1) in (1, 5, 10, 15, 20, 25, len(values)):
            out.append(text(x_pos, chart_y + chart_h + 23, index + 1, "small"))
    out += [
        text(chart_x, chart_y - 12, f"max {maximum:.3f} ms", "small"),
        text(chart_x, chart_y + chart_h + 48, "occurrence index", "small"),
        rect(1150, 554, 326, 186, "terminal", 12),
        text(1174, 587, "same recovered structure", "label"),
        text(1174, 626, f"{len(values)}", "metric"),
        text(1220, 626, "occurrences", "body"),
        text(1174, 664, f"{minimum:.3f} ms", "metric"),
        text(1320, 664, "minimum", "small"),
        text(1174, 700, f"{mean:.3f} ms", "metric"),
        text(1320, 700, "mean", "small"),
        text(1174, 736, f"{maximum:.3f} ms", "metric"),
        text(1320, 736, "maximum", "small"),
        text(
            64,
            856,
            "Structure supplies the statistical condition; provenance keeps "
            "every conclusion auditable.",
            "subtitle",
        ),
        text(1390, 856, "TraceLoom", "mono-hot"),
        "</svg>",
    ]
    return "\n".join(out) + "\n"


def sql_literal(value: object) -> str:
    return "'" + str(value).replace("'", "''") + "'"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("analysis_db", type=Path)
    parser.add_argument("output_svg", type=Path)
    parser.add_argument("--theme", choices=("dark", "paper"), default="dark")
    args = parser.parse_args()
    args.output_svg.parent.mkdir(parents=True, exist_ok=True)
    args.output_svg.write_text(
        render(args.analysis_db, theme=args.theme), encoding="utf-8"
    )


if __name__ == "__main__":
    main()
