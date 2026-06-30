#pragma once

#include <string>
#include <utility>
#include <vector>

namespace traceloom {

enum class DiagnosticSeverity {
  kInfo,
  kWarning,
  kError,
};

struct Diagnostic {
  DiagnosticSeverity severity = DiagnosticSeverity::kInfo;
  std::string code;
  std::string message;
};

class DiagnosticSink {
 public:
  void add(DiagnosticSeverity severity, std::string code, std::string message);

  bool empty() const noexcept { return diagnostics_.empty(); }
  bool has_errors() const noexcept;
  const std::vector<Diagnostic>& entries() const noexcept {
    return diagnostics_;
  }

 private:
  std::vector<Diagnostic> diagnostics_;
};

}  // namespace traceloom
