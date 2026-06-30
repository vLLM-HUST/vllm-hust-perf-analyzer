#pragma once

#include "traceloom/core/string_table.h"
#include "traceloom/ir/anchor_table.h"
#include "traceloom/ir/capture_slot_table.h"
#include "traceloom/ir/communication_op_table.h"
#include "traceloom/ir/graph_template_table.h"
#include "traceloom/ir/protected_interval_table.h"
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
  ReplayUnitTable replay_units;
};

}  // namespace traceloom
