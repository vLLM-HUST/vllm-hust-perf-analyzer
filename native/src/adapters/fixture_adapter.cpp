#include "traceloom/adapters/fixture_adapter.h"

#include <stdexcept>
#include <utility>

namespace traceloom {

FixtureAdapter::FixtureAdapter(FixtureInput input) : input_(std::move(input)) {}

NativeIr FixtureAdapter::load() const {
  NativeIr ir;
  const SourceRefId source_ref = ir.source_refs.append(
      input_.source_kind, input_.source_path, "fixture_tokens", 0);

  for (std::size_t index = 0; index < input_.tokens.size(); ++index) {
    const FixtureToken& token = input_.tokens[index];
    const SymbolId symbol = ir.symbols.intern(token.symbol);
    const TraceEventId event = ir.trace_events.append(
        source_ref, index, token.device_id, token.stream_id, token.start_ns,
        token.end_ns, symbol);
    const AnchorId anchor = ir.anchors.append(
        source_ref, event, ReplayUnitId::invalid(), token.anchor_kind, symbol,
        token.device_id, token.stream_id, token.start_ns, token.end_ns);
    ir.tokens.append(anchor, symbol, token.device_id,
                     static_cast<std::uint32_t>(index), token.start_ns,
                     token.end_ns);
  }

  for (const FixtureProtectedInterval& interval :
       input_.protected_intervals) {
    if (interval.first_token_index > interval.last_token_index ||
        interval.last_token_index >= input_.tokens.size()) {
      throw std::out_of_range("fixture protected interval token range invalid");
    }

    const TokenId first_token(
        static_cast<TokenId::value_type>(interval.first_token_index));
    const TokenId last_token(
        static_cast<TokenId::value_type>(interval.last_token_index));
    const AnchorId first_anchor(
        static_cast<AnchorId::value_type>(interval.first_token_index));
    const AnchorId last_anchor(
        static_cast<AnchorId::value_type>(interval.last_token_index));
    ir.protected_intervals.append(interval.kind, interval.boundary_policy,
                                  first_token, last_token, first_anchor,
                                  last_anchor, source_ref);
  }

  return ir;
}

}  // namespace traceloom
