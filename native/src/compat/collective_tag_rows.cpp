#include "traceloom/compat/collective_tag_rows.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "traceloom/compat/timeline_rows.h"

namespace traceloom::compat {
namespace {

struct AnchorEvidence {
  const VizNodeAnchorSqlRow* coverage = nullptr;
  const AnchorSqlRow* anchor = nullptr;
  const EventSqlRow* event = nullptr;
};

struct LoopCandidate {
  std::string db_name;
  std::uint32_t db_idx = 0;
  std::uint32_t device_id = 0;
  std::string member_id;
  std::string node_id;
  std::string local_node_id;
  std::uint32_t repeat_count = 0;
  std::uint32_t occurrence_count = 0;
  std::uint32_t anchor_count = 0;
  std::uint32_t anchors_per_occurrence = 0;
  std::uint32_t first_anchor_idx = 0;
  std::uint32_t level = 0;
  std::string path;
  std::string collective_pattern;
  std::string signature;
  std::uint32_t signature_ordinal = 0;
  std::string pair_id;
};

std::string lower_ascii(std::string value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

std::string compact_token(const std::string& value) {
  std::string out;
  bool last_was_sep = false;
  for (char ch : value) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch)) {
      out.push_back(ch);
      last_was_sep = false;
    } else if (!last_was_sep && !out.empty()) {
      out.push_back('_');
      last_was_sep = true;
    }
  }
  while (!out.empty() && out.back() == '_') {
    out.pop_back();
  }
  return out;
}

std::string normalize_op_type(const std::string& family,
                              const std::string& label) {
  const std::string blob = lower_ascii(family + " " + label);
  if (blob.find("allreduce") != std::string::npos ||
      blob.find("all_reduce") != std::string::npos) {
    return "allReduce";
  }
  if (blob.find("allgather") != std::string::npos ||
      blob.find("all_gather") != std::string::npos) {
    return "allGather";
  }
  if (blob.find("alltoall") != std::string::npos ||
      blob.find("all_to_all") != std::string::npos ||
      blob.find("all2all") != std::string::npos) {
    return "allToAll";
  }
  if (blob.find("reducescatter") != std::string::npos ||
      blob.find("reduce_scatter") != std::string::npos) {
    return "reduceScatter";
  }
  if (blob.find("broadcast") != std::string::npos) {
    return "broadcast";
  }
  std::string compact = compact_token(!family.empty() ? family : label);
  return compact.empty() ? "collective" : compact;
}

bool op_type_looks_collective(const std::string& op_type) {
  return op_type == "allReduce" || op_type == "allGather" ||
         op_type == "allToAll" || op_type == "reduceScatter" ||
         op_type == "broadcast";
}

std::string event_family(const EventSqlRow& event, const AnchorSqlRow& anchor) {
  if (!event.family.empty()) {
    return event.family;
  }
  if (!event.op_type.empty()) {
    return event.op_type;
  }
  return anchor.family;
}

std::string event_label(const EventSqlRow& event, const AnchorSqlRow& anchor) {
  if (!event.label.empty()) {
    return event.label;
  }
  if (!event.raw_label.empty()) {
    return event.raw_label;
  }
  if (!event.symbol.empty()) {
    return event.symbol;
  }
  return anchor.label;
}

std::string event_role(const EventSqlRow& event, const AnchorSqlRow& anchor) {
  if (!event.role.empty()) {
    return event.role;
  }
  return anchor.role;
}

bool is_collective_event(const EventSqlRow& event, const AnchorSqlRow& anchor) {
  const std::string role = lower_ascii(event_role(event, anchor));
  if (role == "collective") {
    return true;
  }
  const std::string semantic_role = lower_ascii(event.semantic_role);
  if (semantic_role == "collective") {
    return true;
  }
  const std::string family = event_family(event, anchor);
  const std::string label = event_label(event, anchor);
  const std::string blob = lower_ascii(family + " " + label + " " +
                                       event.symbol + " " + anchor.symbol);
  return (role == "comm" || role == "communication" || role.empty()) &&
         (blob.find("hccl") != std::string::npos ||
          op_type_looks_collective(normalize_op_type(family, label)));
}

bool is_repeat_node(const VizNodeSqlRow& node) {
  return node.kind == "repeat" || node.node_type == "Repeat";
}

std::string member_id(const std::string& db_name,
                      std::uint32_t db_idx,
                      std::uint32_t device_id) {
  std::ostringstream out;
  out << db_name << ":db" << std::setw(2) << std::setfill('0') << db_idx
      << ":dev" << device_id;
  return out.str();
}

std::string sanitize_run_name(const std::string& value) {
  std::string out;
  bool last_was_sep = false;
  for (char ch : value) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch) || ch == '_' || ch == '.' || ch == '-') {
      out.push_back(ch);
      last_was_sep = false;
    } else if (!last_was_sep) {
      out.push_back('_');
      last_was_sep = true;
    }
  }
  while (!out.empty() && out.front() == '_') {
    out.erase(out.begin());
  }
  while (!out.empty() && out.back() == '_') {
    out.pop_back();
  }
  return out.empty() ? "traceloom_run" : out;
}

std::string join_with(const std::vector<std::string>& values,
                      const std::string& delimiter) {
  std::string out;
  for (std::size_t idx = 0; idx < values.size(); ++idx) {
    if (idx > 0) {
      out += delimiter;
    }
    out += values[idx];
  }
  return out;
}

std::string join_unique(const std::vector<std::string>& values) {
  std::set<std::string> unique;
  for (const std::string& value : values) {
    if (!value.empty()) {
      unique.insert(value);
    }
  }
  return join_with(std::vector<std::string>(unique.begin(), unique.end()), " ");
}

std::string candidate_collective_key(const std::string& run_name,
                                     const std::string& pair_id,
                                     std::uint32_t occurrence_idx,
                                     const std::string& op_type,
                                     std::uint32_t idx_in_occurrence) {
  std::ostringstream out;
  out << run_name << ':' << pair_id << ":occ_" << std::setw(6)
      << std::setfill('0') << occurrence_idx << ':' << op_type << ":idx_"
      << std::setw(4) << std::setfill('0') << idx_in_occurrence;
  return out.str();
}

std::uint32_t left_rotate(std::uint32_t value, unsigned bits) {
  return (value << bits) | (value >> (32U - bits));
}

std::string sha1_hex(const std::string& input) {
  std::vector<std::uint8_t> message(input.begin(), input.end());
  const std::uint64_t bit_len =
      static_cast<std::uint64_t>(message.size()) * 8U;
  message.push_back(0x80);
  while ((message.size() % 64U) != 56U) {
    message.push_back(0);
  }
  for (int shift = 56; shift >= 0; shift -= 8) {
    message.push_back(static_cast<std::uint8_t>((bit_len >> shift) & 0xffU));
  }

  std::uint32_t h0 = 0x67452301U;
  std::uint32_t h1 = 0xEFCDAB89U;
  std::uint32_t h2 = 0x98BADCFEU;
  std::uint32_t h3 = 0x10325476U;
  std::uint32_t h4 = 0xC3D2E1F0U;

  for (std::size_t chunk = 0; chunk < message.size(); chunk += 64U) {
    std::array<std::uint32_t, 80> w{};
    for (std::size_t idx = 0; idx < 16U; ++idx) {
      const std::size_t offset = chunk + idx * 4U;
      w[idx] = (static_cast<std::uint32_t>(message[offset]) << 24U) |
               (static_cast<std::uint32_t>(message[offset + 1U]) << 16U) |
               (static_cast<std::uint32_t>(message[offset + 2U]) << 8U) |
               static_cast<std::uint32_t>(message[offset + 3U]);
    }
    for (std::size_t idx = 16U; idx < 80U; ++idx) {
      w[idx] = left_rotate(w[idx - 3U] ^ w[idx - 8U] ^ w[idx - 14U] ^
                               w[idx - 16U],
                           1);
    }

    std::uint32_t a = h0;
    std::uint32_t b = h1;
    std::uint32_t c = h2;
    std::uint32_t d = h3;
    std::uint32_t e = h4;
    for (std::size_t idx = 0; idx < 80U; ++idx) {
      std::uint32_t f = 0;
      std::uint32_t k = 0;
      if (idx < 20U) {
        f = (b & c) | ((~b) & d);
        k = 0x5A827999U;
      } else if (idx < 40U) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1U;
      } else if (idx < 60U) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDCU;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6U;
      }
      const std::uint32_t temp = left_rotate(a, 5) + f + e + k + w[idx];
      e = d;
      d = c;
      c = left_rotate(b, 30);
      b = a;
      a = temp;
    }
    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
  }

  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::nouppercase;
  for (std::uint32_t word : {h0, h1, h2, h3, h4}) {
    out << std::setw(8) << word;
  }
  return out.str();
}

std::vector<std::string> primitive_motif(
    const std::vector<std::string>& tokens) {
  if (tokens.empty()) {
    return {};
  }
  for (std::size_t period = 1; period <= tokens.size(); ++period) {
    if (tokens.size() % period != 0U) {
      continue;
    }
    bool matches = true;
    for (std::size_t idx = 0; idx < tokens.size(); ++idx) {
      if (tokens[idx] != tokens[idx % period]) {
        matches = false;
        break;
      }
    }
    if (matches) {
      return std::vector<std::string>(tokens.begin(), tokens.begin() + period);
    }
  }
  return tokens;
}

std::string loop_signature(const std::vector<std::string>& anchor_tokens,
                           const std::string& collective_pattern) {
  const std::vector<std::string> motif = primitive_motif(anchor_tokens);
  std::string payload;
  std::uint32_t motif_len = 0;
  if (!motif.empty()) {
    payload = "motif=" + join_with(motif, "|");
    motif_len = static_cast<std::uint32_t>(motif.size());
  } else {
    payload = "collectives=" + collective_pattern;
  }
  std::ostringstream out;
  out << 'M' << std::setw(4) << std::setfill('0') << motif_len << ':'
      << sha1_hex(payload).substr(0, 10);
  return out.str();
}

std::uint32_t motif_len_from_signature(const std::string& signature) {
  if (signature.size() < 6 || signature[0] != 'M') {
    return 0;
  }
  return static_cast<std::uint32_t>(std::stoul(signature.substr(1, 4)));
}

std::string pair_id(const LoopCandidate& loop, std::uint32_t ordinal) {
  const std::string::size_type colon = loop.signature.find(':');
  const std::string digest =
      colon == std::string::npos ? "" : loop.signature.substr(colon + 1, 6);
  std::ostringstream out;
  out << "LP_M" << std::setw(3) << std::setfill('0')
      << motif_len_from_signature(loop.signature) << '_' << std::setw(2)
      << std::setfill('0') << ordinal << '_' << digest;
  return out.str();
}

double round3(double value) { return std::round(value * 1000.0) / 1000.0; }

std::map<std::string, const EventSqlRow*> index_events(
    const std::vector<EventSqlRow>& rows) {
  std::map<std::string, const EventSqlRow*> out;
  for (const EventSqlRow& row : rows) {
    out[row.event_id] = &row;
  }
  return out;
}

std::map<std::string, const AnchorSqlRow*> index_anchors(
    const std::vector<AnchorSqlRow>& rows) {
  std::map<std::string, const AnchorSqlRow*> out;
  for (const AnchorSqlRow& row : rows) {
    out[row.anchor_id] = &row;
  }
  return out;
}

std::map<std::string, const VizNodeSqlRow*> index_nodes(
    const std::vector<VizNodeSqlRow>& rows) {
  std::map<std::string, const VizNodeSqlRow*> out;
  for (const VizNodeSqlRow& row : rows) {
    out[row.node_id] = &row;
  }
  return out;
}

std::map<std::string, std::vector<const VizNodeAnchorSqlRow*>>
coverage_by_node(const std::vector<VizNodeAnchorSqlRow>& rows) {
  std::map<std::string, std::vector<const VizNodeAnchorSqlRow*>> out;
  for (const VizNodeAnchorSqlRow& row : rows) {
    out[row.node_id].push_back(&row);
  }
  return out;
}

std::vector<AnchorEvidence> resolve_coverage(
    const std::vector<const VizNodeAnchorSqlRow*>& coverage,
    const std::map<std::string, const AnchorSqlRow*>& anchors_by_id,
    const std::map<std::string, const EventSqlRow*>& events_by_id) {
  std::vector<AnchorEvidence> out;
  for (const VizNodeAnchorSqlRow* coverage_row : coverage) {
    const auto anchor_it = anchors_by_id.find(coverage_row->anchor_id);
    if (anchor_it == anchors_by_id.end()) {
      continue;
    }
    const AnchorSqlRow* anchor = anchor_it->second;
    const auto event_it = events_by_id.find(anchor->event_id);
    if (event_it == events_by_id.end()) {
      continue;
    }
    out.push_back({coverage_row, anchor, event_it->second});
  }
  return out;
}

std::uint32_t min_occurrence_idx(const std::vector<AnchorEvidence>& evidence) {
  std::uint32_t out = std::numeric_limits<std::uint32_t>::max();
  for (const AnchorEvidence& item : evidence) {
    out = std::min(out, item.coverage->occurrence_idx);
  }
  return out == std::numeric_limits<std::uint32_t>::max() ? 0 : out;
}

void sort_evidence(std::vector<AnchorEvidence>& evidence) {
  std::sort(evidence.begin(), evidence.end(),
            [](const AnchorEvidence& lhs, const AnchorEvidence& rhs) {
              return std::make_tuple(lhs.coverage->anchor_order,
                                     lhs.event->start_ns, lhs.anchor->anchor_idx,
                                     lhs.anchor->anchor_id) <
                     std::make_tuple(rhs.coverage->anchor_order,
                                     rhs.event->start_ns, rhs.anchor->anchor_idx,
                                     rhs.anchor->anchor_id);
            });
}

std::vector<LoopCandidate> load_loop_candidates(
    const CollectiveTagMemberInput& input) {
  const auto events_by_id = index_events(input.events);
  const auto anchors_by_id = index_anchors(input.anchors);
  const auto coverage = coverage_by_node(input.node_anchor_coverage.node_anchors);
  std::vector<LoopCandidate> loops;

  for (const VizNodeSqlRow& node : input.loop_tree.nodes) {
    if (!is_repeat_node(node)) {
      continue;
    }
    const auto coverage_it = coverage.find(node.node_id);
    if (coverage_it == coverage.end()) {
      continue;
    }
    std::vector<AnchorEvidence> evidence =
        resolve_coverage(coverage_it->second, anchors_by_id, events_by_id);
    bool has_collective = false;
    for (const AnchorEvidence& item : evidence) {
      if (is_collective_event(*item.event, *item.anchor)) {
        has_collective = true;
        break;
      }
    }
    if (!has_collective) {
      continue;
    }

    const std::uint32_t occurrence = min_occurrence_idx(evidence);
    std::vector<AnchorEvidence> occurrence_evidence;
    for (const AnchorEvidence& item : evidence) {
      if (item.coverage->occurrence_idx == occurrence) {
        occurrence_evidence.push_back(item);
      }
    }
    sort_evidence(occurrence_evidence);

    std::vector<std::string> anchor_tokens;
    std::vector<std::string> collective_pattern_items;
    for (const AnchorEvidence& item : occurrence_evidence) {
      const std::string family = event_family(*item.event, *item.anchor);
      std::string label = event_label(*item.event, *item.anchor);
      std::string role = event_role(*item.event, *item.anchor);
      if (is_collective_event(*item.event, *item.anchor)) {
        role = "collective";
        label = normalize_op_type(family, label);
        std::ostringstream pattern_item;
        pattern_item << item.coverage->anchor_order << ':' << label;
        collective_pattern_items.push_back(pattern_item.str());
      }
      anchor_tokens.push_back(role + ":" + family + ":" + label);
    }

    std::uint32_t anchors_per_occurrence =
        static_cast<std::uint32_t>(node.anchors_per_occurrence);
    if (anchors_per_occurrence == 0 && node.occurrence_count > 0) {
      anchors_per_occurrence = std::max<std::uint32_t>(
          1, static_cast<std::uint32_t>(
                 std::lround(static_cast<double>(node.anchor_count) /
                             static_cast<double>(node.occurrence_count))));
    }

    const std::uint32_t db_idx = node.db_idx;
    const std::uint32_t device_id = node.device_id;
    LoopCandidate loop;
    loop.db_name = input.db_name;
    loop.db_idx = db_idx;
    loop.device_id = device_id;
    loop.member_id = member_id(input.db_name, db_idx, device_id);
    loop.node_id = node.node_id;
    loop.local_node_id = node.local_node_id;
    loop.repeat_count = node.repeat_count;
    loop.occurrence_count = node.occurrence_count;
    loop.anchor_count = node.anchor_count;
    loop.anchors_per_occurrence = anchors_per_occurrence;
    loop.first_anchor_idx = node.first_anchor_idx;
    loop.level = node.level;
    loop.path = node.path;
    loop.collective_pattern = join_with(collective_pattern_items, ",");
    loop.signature = loop_signature(anchor_tokens, loop.collective_pattern);
    loops.push_back(std::move(loop));
  }
  std::sort(loops.begin(), loops.end(), [](const LoopCandidate& lhs,
                                           const LoopCandidate& rhs) {
    return std::make_tuple(lhs.db_idx, lhs.device_id, lhs.first_anchor_idx,
                           lhs.local_node_id) <
           std::make_tuple(rhs.db_idx, rhs.device_id, rhs.first_anchor_idx,
                           rhs.local_node_id);
  });
  return loops;
}

std::vector<LoopCandidate> assign_loop_pairs(
    const std::vector<LoopCandidate>& loops) {
  std::map<std::string, std::vector<LoopCandidate>> by_signature;
  for (const LoopCandidate& loop : loops) {
    by_signature[loop.signature].push_back(loop);
  }

  std::vector<LoopCandidate> out;
  for (const auto& signature_entry : by_signature) {
    std::map<std::string, std::vector<LoopCandidate>> by_member;
    for (const LoopCandidate& loop : signature_entry.second) {
      by_member[loop.member_id].push_back(loop);
    }
    std::size_t max_ordinal = 0;
    for (auto& member_entry : by_member) {
      std::sort(member_entry.second.begin(), member_entry.second.end(),
                [](const LoopCandidate& lhs, const LoopCandidate& rhs) {
                  return std::make_tuple(lhs.first_anchor_idx,
                                         lhs.local_node_id) <
                         std::make_tuple(rhs.first_anchor_idx,
                                         rhs.local_node_id);
                });
      max_ordinal = std::max(max_ordinal, member_entry.second.size());
    }
    for (std::size_t ordinal = 1; ordinal <= max_ordinal; ++ordinal) {
      std::vector<LoopCandidate*> candidates;
      for (auto& member_entry : by_member) {
        if (member_entry.second.size() >= ordinal) {
          candidates.push_back(&member_entry.second[ordinal - 1]);
        }
      }
      if (candidates.empty()) {
        continue;
      }
      const std::string assigned_pair_id =
          pair_id(*candidates.front(), static_cast<std::uint32_t>(ordinal));
      for (LoopCandidate* loop : candidates) {
        LoopCandidate assigned = *loop;
        assigned.signature_ordinal = static_cast<std::uint32_t>(ordinal);
        assigned.pair_id = assigned_pair_id;
        out.push_back(std::move(assigned));
      }
    }
  }
  std::sort(out.begin(), out.end(), [](const LoopCandidate& lhs,
                                       const LoopCandidate& rhs) {
    return std::make_tuple(lhs.pair_id, lhs.db_name, lhs.device_id,
                           lhs.first_anchor_idx) <
           std::make_tuple(rhs.pair_id, rhs.db_name, rhs.device_id,
                           rhs.first_anchor_idx);
  });
  return out;
}

struct OwnerKey {
  std::string anchor_id;
  std::uint32_t occurrence_idx = 0;

  bool operator<(const OwnerKey& rhs) const {
    return std::make_tuple(anchor_id, occurrence_idx) <
           std::make_tuple(rhs.anchor_id, rhs.occurrence_idx);
  }
};

struct OwnerValue {
  const VizNodeAnchorSqlRow* coverage = nullptr;
  const VizNodeSqlRow* node = nullptr;
  const AnchorSqlRow* anchor = nullptr;
  const EventSqlRow* event = nullptr;
};

std::tuple<std::uint32_t, std::uint32_t, std::string> owner_rank(
    const VizNodeSqlRow& node) {
  const std::uint32_t anchor_count =
      node.anchor_count == 0 ? std::numeric_limits<std::uint32_t>::max()
                             : node.anchor_count;
  return std::make_tuple(anchor_count,
                         std::numeric_limits<std::uint32_t>::max() -
                             node.level,
                         node.local_node_id);
}

std::vector<CollectiveGlobalLinkSqlRow> build_links_for_member(
    const CollectiveTagMemberInput& input,
    const std::string& run_name,
    const std::map<std::pair<std::string, std::string>, std::string>&
        pair_by_node) {
  const auto events_by_id = index_events(input.events);
  const auto anchors_by_id = index_anchors(input.anchors);
  const auto nodes_by_id = index_nodes(input.loop_tree.nodes);

  std::map<OwnerKey, OwnerValue> best_owner;
  for (const VizNodeAnchorSqlRow& coverage :
       input.node_anchor_coverage.node_anchors) {
    const auto node_it = nodes_by_id.find(coverage.node_id);
    if (node_it == nodes_by_id.end() || !is_repeat_node(*node_it->second)) {
      continue;
    }
    const auto anchor_it = anchors_by_id.find(coverage.anchor_id);
    if (anchor_it == anchors_by_id.end()) {
      continue;
    }
    const auto event_it = events_by_id.find(anchor_it->second->event_id);
    if (event_it == events_by_id.end()) {
      continue;
    }
    if (!is_collective_event(*event_it->second, *anchor_it->second)) {
      continue;
    }
    const OwnerKey key{coverage.anchor_id, coverage.occurrence_idx};
    const OwnerValue candidate{&coverage, node_it->second, anchor_it->second,
                               event_it->second};
    const auto current = best_owner.find(key);
    if (current == best_owner.end() ||
        owner_rank(*candidate.node) < owner_rank(*current->second.node)) {
      best_owner[key] = candidate;
    }
  }

  std::map<std::tuple<std::string, std::uint32_t, std::string, std::uint32_t,
                      std::string>,
           std::vector<OwnerValue>>
      grouped;
  for (const auto& owner_entry : best_owner) {
    const OwnerValue& owner = owner_entry.second;
    const auto pair_it =
        pair_by_node.find(std::make_pair(input.db_name, owner.node->node_id));
    if (pair_it == pair_by_node.end()) {
      continue;
    }
    const std::string family = event_family(*owner.event, *owner.anchor);
    const std::string label = event_label(*owner.event, *owner.anchor);
    const std::string op_type = normalize_op_type(family, label);
    const std::uint32_t device_id = owner.node->device_id;
    grouped[std::make_tuple(input.db_name, device_id, owner.node->local_node_id,
                            owner.coverage->occurrence_idx, op_type)]
        .push_back(owner);
  }

  std::vector<CollectiveGlobalLinkSqlRow> links;
  for (auto& group_entry : grouped) {
    std::vector<OwnerValue>& rows = group_entry.second;
    std::sort(rows.begin(), rows.end(),
              [](const OwnerValue& lhs, const OwnerValue& rhs) {
                return std::make_tuple(lhs.coverage->anchor_order,
                                       lhs.event->start_ns,
                                       lhs.anchor->anchor_id) <
                       std::make_tuple(rhs.coverage->anchor_order,
                                       rhs.event->start_ns,
                                       rhs.anchor->anchor_id);
              });

    for (std::size_t idx = 0; idx < rows.size(); ++idx) {
      const OwnerValue& owner = rows[idx];
      const std::string op_type = std::get<4>(group_entry.first);
      const std::string& pair =
          pair_by_node.at(std::make_pair(input.db_name, owner.node->node_id));
      CollectiveGlobalLinkSqlRow row;
      row.candidate_collective_key = candidate_collective_key(
          run_name, pair, owner.coverage->occurrence_idx, op_type,
          static_cast<std::uint32_t>(idx + 1));
      row.db_name = input.db_name;
      row.db_idx = owner.node->db_idx;
      row.device_id = owner.node->device_id;
      row.member_id = member_id(input.db_name, row.db_idx, row.device_id);
      row.pair_id = pair;
      row.local_node_id = owner.node->local_node_id;
      row.occurrence_idx = owner.coverage->occurrence_idx;
      row.idx_in_occurrence = static_cast<std::uint32_t>(idx + 1);
      row.op_type = op_type;
      row.anchor_id = owner.anchor->anchor_id;
      row.event_id = owner.anchor->event_id;
      row.source_table = owner.event->source_table;
      row.source_key = owner.event->source_key;
      row.connection_id.clear();
      row.op_id.clear();
      row.start_ns = owner.event->start_ns;
      row.end_ns = owner.event->end_ns;
      row.dur_us = owner.event->dur_us;
      row.validation_status = "candidate";
      row.confidence = 0.5;
      links.push_back(std::move(row));
    }
  }

  std::sort(links.begin(), links.end(),
            [](const CollectiveGlobalLinkSqlRow& lhs,
               const CollectiveGlobalLinkSqlRow& rhs) {
              return std::make_tuple(lhs.candidate_collective_key, lhs.db_name,
                                     lhs.device_id, lhs.local_node_id,
                                     lhs.anchor_id) <
                     std::make_tuple(rhs.candidate_collective_key, rhs.db_name,
                                     rhs.device_id, rhs.local_node_id,
                                     rhs.anchor_id);
            });
  return links;
}

GlobalCollectiveMemberSqlRow member_from_link(
    const CollectiveGlobalLinkSqlRow& link) {
  GlobalCollectiveMemberSqlRow row;
  row.candidate_collective_key = link.candidate_collective_key;
  row.db_name = link.db_name;
  row.db_idx = link.db_idx;
  row.device_id = link.device_id;
  row.member_id = link.member_id;
  row.pair_id = link.pair_id;
  row.local_node_id = link.local_node_id;
  row.occurrence_idx = link.occurrence_idx;
  row.idx_in_occurrence = link.idx_in_occurrence;
  row.op_type = link.op_type;
  row.anchor_id = link.anchor_id;
  row.event_id = link.event_id;
  row.source_table = link.source_table;
  row.source_key = link.source_key;
  row.connection_id = link.connection_id;
  row.op_id = link.op_id;
  row.start_ns = link.start_ns;
  row.end_ns = link.end_ns;
  row.dur_us = link.dur_us;
  row.validation_status = link.validation_status;
  row.confidence = link.confidence;
  return row;
}

std::vector<GlobalCollectiveSummarySqlRow> summarize_links(
    std::vector<CollectiveGlobalLinkSqlRow>& links,
    const std::set<std::string>& expected_members,
    std::uint32_t expected_world_size) {
  std::map<std::string, std::vector<CollectiveGlobalLinkSqlRow*>> grouped;
  for (CollectiveGlobalLinkSqlRow& link : links) {
    grouped[link.candidate_collective_key].push_back(&link);
  }

  std::vector<GlobalCollectiveSummarySqlRow> summaries;
  for (auto& group_entry : grouped) {
    std::vector<CollectiveGlobalLinkSqlRow*>& group = group_entry.second;
    std::sort(group.begin(), group.end(),
              [](const CollectiveGlobalLinkSqlRow* lhs,
                 const CollectiveGlobalLinkSqlRow* rhs) {
                return std::make_tuple(lhs->db_name, lhs->device_id,
                                       lhs->start_ns, lhs->anchor_id) <
                       std::make_tuple(rhs->db_name, rhs->device_id,
                                       rhs->start_ns, rhs->anchor_id);
              });
    std::set<std::string> member_set;
    std::vector<std::string> connection_ids;
    std::vector<std::string> op_ids;
    std::vector<std::int64_t> starts;
    std::vector<double> durations;
    for (const CollectiveGlobalLinkSqlRow* link : group) {
      member_set.insert(link->member_id);
      connection_ids.push_back(link->connection_id);
      op_ids.push_back(link->op_id);
      if (link->start_ns != 0) {
        starts.push_back(link->start_ns);
      }
      durations.push_back(link->dur_us);
    }
    std::vector<std::string> members(member_set.begin(), member_set.end());
    std::vector<std::string> missing;
    std::set_difference(expected_members.begin(), expected_members.end(),
                        member_set.begin(), member_set.end(),
                        std::back_inserter(missing));
    if (expected_world_size > expected_members.size()) {
      for (std::uint32_t idx = 1;
           idx <= expected_world_size - expected_members.size(); ++idx) {
        missing.push_back("unknown_member_" + std::to_string(idx));
      }
    }

    const std::uint32_t member_count =
        static_cast<std::uint32_t>(member_set.size());
    std::string status;
    double confidence = 0.0;
    if (member_count >= expected_world_size) {
      status = "complete";
      confidence = 0.85;
    } else if (member_count > 1) {
      status = "partial";
      confidence = 0.55;
    } else {
      status = "singleton";
      confidence = 0.35;
    }
    for (CollectiveGlobalLinkSqlRow* link : group) {
      link->validation_status = status;
      link->confidence = confidence;
    }

    double start_skew_us = 0.0;
    if (starts.size() > 1) {
      const auto [min_it, max_it] =
          std::minmax_element(starts.begin(), starts.end());
      start_skew_us =
          (static_cast<double>(*max_it) - static_cast<double>(*min_it)) /
          1000.0;
    }
    double duration_skew_us = 0.0;
    if (durations.size() > 1) {
      const auto [min_it, max_it] =
          std::minmax_element(durations.begin(), durations.end());
      duration_skew_us = *max_it - *min_it;
    }

    const CollectiveGlobalLinkSqlRow* sample = group.front();
    GlobalCollectiveSummarySqlRow summary;
    summary.candidate_collective_key = group_entry.first;
    summary.pair_id = sample->pair_id;
    summary.occurrence_idx = sample->occurrence_idx;
    summary.op_type = sample->op_type;
    summary.idx_in_occurrence = sample->idx_in_occurrence;
    summary.member_count = member_count;
    summary.expected_world_size = expected_world_size;
    summary.start_skew_us = round3(start_skew_us);
    summary.duration_skew_us = round3(duration_skew_us);
    summary.connection_ids = join_unique(connection_ids);
    summary.op_ids = join_unique(op_ids);
    summary.members = join_with(members, " ");
    summary.missing_members = join_with(missing, " ");
    summary.validation_status = status;
    summary.confidence = confidence;
    summaries.push_back(std::move(summary));
  }
  return summaries;
}

std::set<std::string> member_ids_from_input(
    const CollectiveTagMemberInput& input) {
  std::set<std::string> out;
  for (const VizNodeSqlRow& node : input.loop_tree.nodes) {
    out.insert(member_id(input.db_name, node.db_idx, node.device_id));
  }
  if (out.empty()) {
    out.insert(member_id(input.db_name, input.db_idx, input.device_id));
  }
  return out;
}

}  // namespace

CollectiveTagSqlRows build_collective_tag_sql_rows(
    const std::vector<CollectiveTagMemberInput>& members,
    const CollectiveTagOptions& options) {
  const std::string run_name = sanitize_run_name(options.run_name);

  std::vector<LoopCandidate> loops;
  std::set<std::string> expected_members;
  for (const CollectiveTagMemberInput& member : members) {
    if (member.db_name.empty()) {
      throw std::invalid_argument(
          "collective tag member input requires a db_name");
    }
    const std::set<std::string> member_ids = member_ids_from_input(member);
    expected_members.insert(member_ids.begin(), member_ids.end());
    std::vector<LoopCandidate> member_loops = load_loop_candidates(member);
    loops.insert(loops.end(), member_loops.begin(), member_loops.end());
  }

  const std::vector<LoopCandidate> paired_loops = assign_loop_pairs(loops);
  std::map<std::pair<std::string, std::string>, std::string> pair_by_node;
  for (const LoopCandidate& loop : paired_loops) {
    pair_by_node[std::make_pair(loop.db_name, loop.node_id)] = loop.pair_id;
  }

  CollectiveTagSqlRows out;
  for (const CollectiveTagMemberInput& member : members) {
    std::vector<CollectiveGlobalLinkSqlRow> member_links =
        build_links_for_member(member, run_name, pair_by_node);
    out.local_links.insert(out.local_links.end(), member_links.begin(),
                           member_links.end());
  }
  std::sort(out.local_links.begin(), out.local_links.end(),
            [](const CollectiveGlobalLinkSqlRow& lhs,
               const CollectiveGlobalLinkSqlRow& rhs) {
              return std::make_tuple(lhs.candidate_collective_key, lhs.db_name,
                                     lhs.device_id, lhs.local_node_id,
                                     lhs.anchor_id) <
                     std::make_tuple(rhs.candidate_collective_key, rhs.db_name,
                                     rhs.device_id, rhs.local_node_id,
                                     rhs.anchor_id);
            });

  const std::uint32_t expected_world_size =
      options.expected_world_size == 0
          ? std::max<std::uint32_t>(
                1, static_cast<std::uint32_t>(expected_members.size()))
          : options.expected_world_size;
  out.global_rows.summaries =
      summarize_links(out.local_links, expected_members, expected_world_size);
  std::sort(out.global_rows.summaries.begin(), out.global_rows.summaries.end(),
            [](const GlobalCollectiveSummarySqlRow& lhs,
               const GlobalCollectiveSummarySqlRow& rhs) {
              return lhs.candidate_collective_key <
                     rhs.candidate_collective_key;
            });

  for (const CollectiveGlobalLinkSqlRow& link : out.local_links) {
    out.global_rows.members.push_back(member_from_link(link));
  }
  return out;
}

CollectiveTagSqlRows build_graph_body_collective_tag_sql_rows(
    const NativeIr& ir,
    const std::string& db_name,
    std::uint32_t db_idx,
    const CollectiveTagOptions& options) {
  if (db_name.empty()) {
    throw std::invalid_argument(
        "graph-body collective tags require a db_name");
  }

  struct BodyEvidence {
    const GraphLaunchBodyRow* body = nullptr;
    const GraphLaunchOccurrenceRow* launch = nullptr;
    const ReplayBodyTemplateRow* body_template = nullptr;
  };

  std::vector<BodyEvidence> bodies;
  bodies.reserve(ir.graph_launch_bodies.size());
  for (const GraphLaunchBodyRow& body : ir.graph_launch_bodies.rows()) {
    if (!body.graph_launch_occurrence_id.valid() ||
        body.graph_launch_occurrence_id.value() >=
            ir.graph_launch_occurrences.size() ||
        !body.replay_body_template_id.valid() ||
        body.replay_body_template_id.value() >=
            ir.replay_body_templates.size()) {
      throw std::invalid_argument(
          "graph launch body has invalid occurrence or template evidence");
    }
    bodies.push_back(
        BodyEvidence{&body,
                     &ir.graph_launch_occurrences.row(
                         body.graph_launch_occurrence_id),
                     &ir.replay_body_templates.row(
                         body.replay_body_template_id)});
  }
  std::stable_sort(
      bodies.begin(), bodies.end(), [](const BodyEvidence& lhs,
                                       const BodyEvidence& rhs) {
        return std::make_tuple(lhs.launch->device_id,
                               lhs.body_template->exact_sequence_hash,
                               lhs.launch->start_ns, lhs.launch->end_ns,
                               lhs.body->id) <
               std::make_tuple(rhs.launch->device_id,
                               rhs.body_template->exact_sequence_hash,
                               rhs.launch->start_ns, rhs.launch->end_ns,
                               rhs.body->id);
      });

  std::set<std::uint32_t> body_devices;
  std::map<std::uint64_t, std::map<std::uint32_t, std::uint32_t>>
      body_count_by_template_and_device;
  for (const BodyEvidence& evidence : bodies) {
    body_devices.insert(evidence.launch->device_id);
    ++body_count_by_template_and_device
          [evidence.body_template->exact_sequence_hash]
          [evidence.launch->device_id];
  }
  if (body_devices.size() < 2) {
    return CollectiveTagSqlRows{};
  }
  std::map<std::uint64_t, bool> ordinal_alignment_stable;
  for (const auto& template_entry : body_count_by_template_and_device) {
    std::set<std::uint32_t> counts;
    for (std::uint32_t device_id : body_devices) {
      const auto found = template_entry.second.find(device_id);
      counts.insert(found == template_entry.second.end() ? 0
                                                          : found->second);
    }
    ordinal_alignment_stable[template_entry.first] =
        counts.size() == 1 && !counts.empty() && *counts.begin() != 0;
  }

  std::map<GraphLaunchBodyId::value_type,
           std::vector<const GraphLaunchBodyMemberRow*>>
      members_by_body;
  for (const GraphLaunchBodyMemberRow& member :
       ir.graph_launch_body_members.rows()) {
    if (!member.graph_launch_body_id.valid() ||
        member.graph_launch_body_id.value() >= ir.graph_launch_bodies.size() ||
        !member.task_id.valid() || member.task_id.value() >= ir.tasks.size()) {
      throw std::invalid_argument(
          "graph launch body member has invalid body or task evidence");
    }
    members_by_body[member.graph_launch_body_id.value()].push_back(&member);
  }
  for (auto& item : members_by_body) {
    std::stable_sort(
        item.second.begin(), item.second.end(),
        [](const GraphLaunchBodyMemberRow* lhs,
           const GraphLaunchBodyMemberRow* rhs) {
          return std::tie(lhs->lane_ordinal, lhs->task_ordinal, lhs->id) <
                 std::tie(rhs->lane_ordinal, rhs->task_ordinal, rhs->id);
        });
  }

  std::map<TraceEventId::value_type, const CommunicationOpRow*> comm_by_event;
  for (const CommunicationOpRow& comm : ir.communication_ops.rows()) {
    if (comm.trace_event_id.valid()) {
      comm_by_event.emplace(comm.trace_event_id.value(), &comm);
    }
  }

  const std::string run_name = sanitize_run_name(options.run_name);
  std::map<std::pair<std::uint32_t, std::uint64_t>, std::uint32_t>
      next_occurrence_by_device_and_template;
  std::set<std::string> expected_members;
  for (std::uint32_t device_id : body_devices) {
    expected_members.insert(member_id(db_name, db_idx, device_id));
  }
  CollectiveTagSqlRows out;

  for (const BodyEvidence& evidence : bodies) {
    const std::uint32_t device_id = evidence.launch->device_id;
    const std::uint64_t template_hash =
        evidence.body_template->exact_sequence_hash;
    const std::uint32_t occurrence =
        ++next_occurrence_by_device_and_template[
            std::make_pair(device_id, template_hash)];

    std::ostringstream hash_text;
    hash_text << std::hex << std::setw(16) << std::setfill('0')
              << template_hash;
    std::string pair = "GB_H" + hash_text.str();
    if (!ordinal_alignment_stable.at(template_hash)) {
      // Unequal per-device occurrence counts make ordinal alignment
      // ambiguous.  Keep the members as typed singletons rather than shifting
      // later occurrences into plausible-looking cross-device pairs.
      pair += "_D" + std::to_string(device_id);
    }

    std::map<std::string, std::uint32_t> next_index_by_op;
    const auto body_members = members_by_body.find(evidence.body->id.value());
    if (body_members == members_by_body.end()) {
      continue;
    }
    for (const GraphLaunchBodyMemberRow* member : body_members->second) {
      if (member->kind !=
          GraphLaunchBodyMemberRow::Kind::kCommunication) {
        continue;
      }
      const TaskRow& task = ir.tasks.row(member->task_id);
      if (!task.trace_event_id.valid() ||
          task.trace_event_id.value() >= ir.trace_events.size()) {
        throw std::invalid_argument(
            "graph-body collective task has invalid trace event evidence");
      }
      const TraceEventRow& event = ir.trace_events.row(task.trace_event_id);
      const SourceRefRow& source = ir.source_refs.row(event.source_ref_id);

      std::string label;
      for (SymbolId symbol : {task.comm_name_symbol_id,
                              task.op_type_symbol_id,
                              task.op_name_symbol_id,
                              event.raw_name_symbol_id}) {
        if (symbol.valid()) {
          label = ir.symbols.value(symbol);
          if (!label.empty()) {
            break;
          }
        }
      }
      const std::string op_type = normalize_op_type("collective", label);
      const std::uint32_t index = ++next_index_by_op[op_type];

      CollectiveGlobalLinkSqlRow row;
      row.candidate_collective_key = candidate_collective_key(
          run_name, pair, occurrence, op_type, index);
      row.db_name = db_name;
      row.db_idx = db_idx;
      row.device_id = device_id;
      row.member_id = member_id(db_name, db_idx, device_id);
      row.pair_id = pair;
      row.local_node_id =
          "graph-body-" + std::to_string(evidence.body->id.value());
      row.occurrence_idx = occurrence;
      row.idx_in_occurrence = index;
      row.op_type = op_type;
      // A graph-body member is deliberately not promoted to a top-level
      // report anchor.  event_id and raw-row lineage remain exact.
      row.anchor_id.clear();
      row.event_id = trace_event_compat_id(task.trace_event_id);
      row.source_table = source.table_name;
      row.source_key = std::to_string(event.source_row_id);
      const auto comm = comm_by_event.find(task.trace_event_id.value());
      if (comm != comm_by_event.end()) {
        if (comm->second->raw_connection_id >= 0) {
          row.connection_id =
              std::to_string(comm->second->raw_connection_id);
        }
        if (comm->second->raw_op_id >= 0) {
          row.op_id = std::to_string(comm->second->raw_op_id);
        }
      }
      row.start_ns = event.start_ns;
      row.end_ns = event.end_ns;
      row.dur_us = static_cast<double>(event.end_ns - event.start_ns) / 1000.0;
      row.validation_status = "candidate";
      row.confidence = 0.5;
      out.local_links.push_back(std::move(row));
    }
  }

  std::sort(out.local_links.begin(), out.local_links.end(),
            [](const CollectiveGlobalLinkSqlRow& lhs,
               const CollectiveGlobalLinkSqlRow& rhs) {
              return std::make_tuple(lhs.candidate_collective_key,
                                     lhs.device_id, lhs.start_ns,
                                     lhs.event_id) <
                     std::make_tuple(rhs.candidate_collective_key,
                                     rhs.device_id, rhs.start_ns,
                                     rhs.event_id);
            });

  const std::uint32_t expected_world_size =
      options.expected_world_size == 0
          ? std::max<std::uint32_t>(
                1, static_cast<std::uint32_t>(expected_members.size()))
          : options.expected_world_size;
  out.global_rows.summaries =
      summarize_links(out.local_links, expected_members, expected_world_size);
  std::sort(out.global_rows.summaries.begin(),
            out.global_rows.summaries.end(),
            [](const GlobalCollectiveSummarySqlRow& lhs,
               const GlobalCollectiveSummarySqlRow& rhs) {
              return lhs.candidate_collective_key <
                     rhs.candidate_collective_key;
            });
  for (const CollectiveGlobalLinkSqlRow& link : out.local_links) {
    out.global_rows.members.push_back(member_from_link(link));
  }
  return out;
}

}  // namespace traceloom::compat
