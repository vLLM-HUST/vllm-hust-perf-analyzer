#pragma once

#include "traceloom/core/string_table.h"
#include "traceloom/ir/anchor_table.h"
#include "traceloom/ir/capture_slot_table.h"
#include "traceloom/ir/captured_graph_instance_table.h"
#include "traceloom/ir/communication_op_table.h"
#include "traceloom/ir/graph_launch_activity_table.h"
#include "traceloom/ir/graph_launch_body_table.h"
#include "traceloom/ir/graph_launch_occurrence_table.h"
#include "traceloom/ir/graph_slot_template_table.h"
#include "traceloom/ir/graph_template_table.h"
#include "traceloom/ir/protected_interval_table.h"
#include "traceloom/ir/replay_composition_candidate_table.h"
#include "traceloom/ir/replay_unit_table.h"
#include "traceloom/ir/source_ref_table.h"
#include "traceloom/ir/stream_table.h"
#include "traceloom/ir/task_table.h"
#include "traceloom/ir/token_table.h"
#include "traceloom/ir/trace_event_table.h"

namespace traceloom {

struct NativeIr {
  StringTable strings;
  SymbolTable symbols;
  SourceRefTable source_refs;
  StreamTable streams;
  TraceEventTable trace_events;
  TaskTable tasks;
  CommunicationOpTable communication_ops;
  AnchorTable anchors;
  TokenTable tokens;
  ProtectedIntervalTable protected_intervals;
  GraphTemplateTable graph_templates;
  CaptureSlotTable capture_slots;
  GraphSlotTemplateTable graph_slot_templates;
  CapturedGraphInstanceTable captured_graph_instances;
  CapturedGraphStreamTable captured_graph_streams;
  GraphLaunchOccurrenceTable graph_launch_occurrences;
  GraphLaunchActivityTable graph_launch_activities;
  GraphLaunchActivityMemberTable graph_launch_activity_members;
  ReplayBodyTemplateTable replay_body_templates;
  GraphLaunchBodyTable graph_launch_bodies;
  GraphLaunchBodyMemberTable graph_launch_body_members;
  ReplayCompositionCandidateTable replay_composition_candidates;
  ReplayCompositionSlotTable replay_composition_slots;
  ReplayCompositionRegionTable replay_composition_regions;
  ReplayCompositionRegionMemberTable replay_composition_region_members;
  ReplayUnitTable replay_units;
  ReplayUnitLaunchMemberTable replay_unit_launch_members;
};

}  // namespace traceloom
