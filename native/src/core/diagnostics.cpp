#include "traceloom/core/diagnostics.h"

#include <algorithm>

namespace traceloom {

void DiagnosticSink::add(DiagnosticSeverity severity,
                         std::string code,
                         std::string message) {
  diagnostics_.push_back(
      Diagnostic{severity, std::move(code), std::move(message)});
}

bool DiagnosticSink::has_errors() const noexcept {
  return std::any_of(diagnostics_.begin(), diagnostics_.end(),
                     [](const Diagnostic& diagnostic) {
                       return diagnostic.severity == DiagnosticSeverity::kError;
                     });
}

}  // namespace traceloom
