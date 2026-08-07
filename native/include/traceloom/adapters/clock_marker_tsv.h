#pragma once

#include <cstddef>
#include <string>

#include "traceloom/ir/native_ir.h"

namespace traceloom {

struct ClockMarkerTsvLoadResult {
  std::size_t marker_count = 0;
  std::size_t rejected_marker_count = 0;
  SourceRefId source_ref_id;
};

// Imports the frozen section-7.2 marker payload. The TSV header is exact:
// marker_id, host_before_ns, host_after_ns, device_timestamp_ns, host_pid,
// host_tid, device_id, stream_id, connection_id, call_site, return_status.
// stream_id and connection_id may be blank; every other field is required.
ClockMarkerTsvLoadResult load_clock_marker_tsv(const std::string& path,
                                               NativeIr& ir);

// Resolves runtime-collected host brackets to the profiler device domain.
// The exact raw header is:
// marker_id, host_before_ns, host_after_ns, host_pid, host_tid, device_id,
// stream_id, call_site, return_status.
//
// A successful bracket is normally identified by exactly one overlapping
// same-thread aclrtRecordEvent host row. Non-empty overlap is intentional because
// msprof's host timestamps can differ from a caller CLOCK_REALTIME bracket by
// enough to remove direct overlap in long runs. In that case resolution is
// allowed only for an order-preserving bijection: successful bracket and
// same-thread record counts must match, timestamps must be strictly ordered,
// and after an endpoint-affine host-clock correction every API must have its
// same-ordinal bracket as the unique nearest bracket. TASK.startNs becomes
// device_timestamp_ns; raw
// aclrtEventGetTimestamp syscnt values are never treated as profiler ns.
// A uniquely resolved API with no connectionId, or a connectionId with no
// matching TASK, is retained as a rejected marker because profiler device
// data can end before its host API tail. Multiple matching APIs/TASKs remain
// fatal ambiguity errors.
ClockMarkerTsvLoadResult resolve_ascend_clock_marker_bracket_tsv(
    const std::string& path,
    NativeIr& ir);

}  // namespace traceloom
