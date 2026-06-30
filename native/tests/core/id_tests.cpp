#include "traceloom/core/diagnostics.h"
#include "traceloom/core/ids.h"
#include "traceloom/core/spans.h"
#include "traceloom/core/status.h"
#include "traceloom/core/string_table.h"
#include "traceloom/testing/test_util.h"

#include <stdexcept>
#include <type_traits>
#include <vector>

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  const TraceEventId event0(0);
  const TraceEventId event1(1);
  const AnchorId anchor0(0);

  static_assert(!std::is_same<TraceEventId, AnchorId>::value,
                "id families must not collapse");

  require(event0.valid());
  require(event0 != event1);
  require(event0 < event1);
  require(anchor0.valid());
  require(!TraceEventId::invalid().valid());

  std::vector<int> values{1, 2, 3};
  Span<int> mutable_span(values);
  mutable_span[1] = 20;
  ArrayView<int> view(values);
  require(view.size() == 3);
  require(view[1] == 20);

  DiagnosticSink sink;
  require(sink.empty());
  sink.add(DiagnosticSeverity::kWarning, "test_warning", "warning message");
  require(!sink.has_errors());
  sink.add(DiagnosticSeverity::kError, "test_error", "error message");
  require(sink.has_errors());

  const Status ok = Status::ok();
  require(ok.ok_status());
  const Status bad(StatusCode::kInvalidArgument, "bad input");
  require(!bad.ok_status());
  require(bad.code() == StatusCode::kInvalidArgument);

  StringTable strings;
  const StringId raw0 = strings.intern("MatMulV2");
  const StringId raw1 = strings.intern("MatMulV2");
  const StringId raw2 = strings.intern("ACLGraph");
  require(raw0 == raw1);
  require(raw0 != raw2);
  require(strings.value(raw0) == "MatMulV2");

  SymbolTable symbols;
  const SymbolId symbol0 = symbols.intern("ACLL");
  const SymbolId symbol1 = symbols.intern("ACLL");
  require(symbol0 == symbol1);
  require(symbols.value(symbol0) == "ACLL");

  bool caught_bad_string = false;
  try {
    (void)strings.value(StringId::invalid());
  } catch (const std::out_of_range&) {
    caught_bad_string = true;
  }
  require(caught_bad_string);

  return 0;
}
