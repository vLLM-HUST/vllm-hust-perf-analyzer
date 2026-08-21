#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "traceloom/analysis/structural_occurrence_graph.h"

namespace traceloom {
struct StructuralProjectionToken;
}

namespace traceloom::compat {

struct PositionRefinementSqlRow {
  std::string parent_position_id;
  std::uint32_t slot_ordinal = 0;
  std::string child_position_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string tree_id;
  std::string view_name;
};

struct PositionOccurrenceSqlRow {
  std::string occurrence_id;
  std::string position_id;
  std::string parent_occurrence_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string tree_id;
  std::string view_name;
  std::uint32_t occurrence_idx = 0;
  std::uint32_t repeat_iteration = 0;
  std::uint32_t token_start_ordinal = 0;
  std::uint32_t token_end_exclusive = 0;
  std::string rooted_position_path;
  std::string occurrence_path;
};

struct PositionMemberSqlRow {
  std::string parent_position_id;
  std::string parent_occurrence_id;
  std::uint32_t slot_ordinal = 0;
  std::uint32_t member_order = 0;
  std::string member_kind;
  std::string child_position_id;
  std::string child_occurrence_id;
  // Zero-based native structural token ordinal. SQL NULL for child members.
  std::int64_t terminal_token_ordinal = -1;
  std::string terminal_anchor_id;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string tree_id;
  std::string view_name;
  std::string member_path;
};

struct StructuralPositionSqlRows {
  std::vector<PositionRefinementSqlRow> refinements;
  std::vector<PositionOccurrenceSqlRow> occurrences;
  std::vector<PositionMemberSqlRow> members;
};

StructuralPositionSqlRows build_structural_position_sql_rows(
    const StructuralOccurrenceGraph& graph,
    const std::vector<StructuralProjectionToken>& tokens,
    std::uint32_t db_idx,
    std::string tree_id,
    std::string view_name,
    bool scope_position_ids_by_device);

void replace_structural_position_rows(
    const std::string& sqlite_path, const StructuralPositionSqlRows& rows);

}  // namespace traceloom::compat
