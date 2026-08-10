#include "traceloom/analysis/audit_report.h"

#include <cstddef>
#include <string>
#include <vector>

namespace traceloom {

std::string diagnostic_code(const std::string& message) {
  const std::size_t colon = message.find(':');
  return message.substr(0, colon);
}

void append_e3_diagnostic_detail(std::string* output,
                                 const StreamStateRunResult& streams) {
  *output += "\n### E3 diagnostic detail\n\n";
  *output += "| device | stream | code | source_row_id | message |\n";
  *output += "| --- | --- | --- | --- | --- |\n";
  const auto append_diagnostics =
      [output](std::string device, std::string stream,
               const std::vector<TimelineDiagnostic>& notes) {
        for (const TimelineDiagnostic& diagnostic : notes) {
          *output += "| " + device + " | " + stream + " | " +
                     diagnostic_code(diagnostic.message) + " | " +
                     std::to_string(diagnostic.source_row_id) + " | " +
                     diagnostic.message + " |\n";
        }
      };
  append_diagnostics("-", "-", streams.diagnostics);
  for (const StreamStateDeviceResult& device : streams.devices) {
    append_diagnostics(std::to_string(device.device_id), "-",
                       device.diagnostics);
    for (const StreamStateTimeline& stream_timeline : device.timelines) {
      append_diagnostics(std::to_string(device.device_id),
                         std::to_string(stream_timeline.stream_id),
                         stream_timeline.diagnostics);
    }
  }
}

}  // namespace traceloom
