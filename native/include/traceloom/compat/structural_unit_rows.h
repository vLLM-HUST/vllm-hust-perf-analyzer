#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/compat/sidecar_writer.h"
#include "traceloom/report/report_tree.h"

namespace traceloom::compat {

struct StructuralUnitSqlRows {
  std::vector<StructuralUnitSqlRow> units;
  std::vector<StructuralUnitAnchorSqlRow> unit_anchors;
};

// Partitions the complete observed anchor sequence around exact graph-unit
// occurrences. Nonempty spans bounded by graph units are complete neutral
// structural units. Open prefix/suffix spans remain typed unrecognized rows.
// Every input token belongs to exactly one returned unit.
StructuralUnitSqlRows build_structural_unit_sql_rows(
    const ReportTree& tree,
    const std::vector<ReportToken>& tokens,
    std::uint32_t db_idx = 0);

}  // namespace traceloom::compat
