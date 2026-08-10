#pragma once

#include <string>

#include "traceloom/analysis/stream_state_timeline.h"

namespace traceloom {

// Diagnostics use "<code>: <detail>" free text (E2/E3 style); the code is the
// part before the first colon.
std::string diagnostic_code(const std::string& message);

// Appends the E3 diagnostic detail section to *output: the section header
// plus one table row per diagnostic in run-, device-, and stream-level
// order. Run-level rows use ("-", "-"), device-level rows use
// ("<device>", "-"), and stream-level rows use ("<device>", "<stream>").
// This is the single renderer shared by the audit report CLI and its
// regression tests.
void append_e3_diagnostic_detail(std::string* output,
                                 const StreamStateRunResult& streams);

}  // namespace traceloom
