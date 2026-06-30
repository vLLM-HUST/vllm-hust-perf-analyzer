#include "traceloom/adapters/fixture_adapter.h"
#include "traceloom/adapters/protected_sequence_fixture_reader.h"
#include "traceloom/analysis/native_pipeline.h"
#include "traceloom/testing/test_util.h"

#include <map>
#include <string>
#include <vector>

namespace {

std::string fixture_path(const std::string& name) {
  return std::string(TRACELOOM_WORKSPACE_ROOT) +
         "/drafts/refactor/80_tests_fixtures/fixtures/protected_sequence/" +
         name + ".json";
}

traceloom::NativePipelineResult run_fixture(
    const traceloom::ProtectedSequenceFixture& fixture,
    std::size_t threads,
    traceloom::NativeIr* out_ir) {
  traceloom::NativeIr ir = traceloom::FixtureAdapter(fixture.input).load();
  traceloom::NativePipelineOptions options;
  options.thread_count = threads;
  options.partition_config = fixture.partition_config;
  options.candidate_scan_config = fixture.candidate_scan_config;
  options.anchor_mode =
      traceloom::NativePipelineAnchorMode::kUseExistingAnchorsAndTokens;
  traceloom::NativePipelineResult result =
      traceloom::run_native_pipeline(ir, options);
  if (out_ir != nullptr) {
    *out_ir = std::move(ir);
  }
  return result;
}

std::string key_to_string(const traceloom::SymbolTable& symbols,
                          const traceloom::CandidateKey& key) {
  std::string text;
  for (std::size_t index = 0; index < key.symbols.size(); ++index) {
    if (index != 0) {
      text += " ";
    }
    text += symbols.value(key.symbols[index]);
  }
  return text;
}

std::map<std::string, std::size_t> candidate_counts(
    const traceloom::SymbolTable& symbols,
    const std::vector<traceloom::CandidateSummaryRow>& rows) {
  std::map<std::string, std::size_t> counts;
  for (const traceloom::CandidateSummaryRow& row : rows) {
    counts.emplace(key_to_string(symbols, row.key), row.occurrence_count);
  }
  return counts;
}

bool summaries_equal(const std::vector<traceloom::CandidateSummaryRow>& lhs,
                     const std::vector<traceloom::CandidateSummaryRow>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    if (!(lhs[index].key == rhs[index].key)) {
      return false;
    }
    if (lhs[index].occurrence_count != rhs[index].occurrence_count) {
      return false;
    }
    if (lhs[index].first_begin != rhs[index].first_begin) {
      return false;
    }
  }
  return true;
}

std::size_t diagnostic_count(
    const traceloom::NativePipelineResult& result,
    traceloom::CandidateDiagnosticCode code) {
  std::size_t count = 0;
  for (const traceloom::CandidateDiagnostic& diagnostic :
       result.pattern_mining_diagnostics.rows) {
    if (diagnostic.code == code) {
      ++count;
    }
  }
  return count;
}

}  // namespace

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  {
    NativeIr ir;
    const ProtectedSequenceFixture fixture =
        load_protected_sequence_fixture(fixture_path("f1_tiny_repeated_pair"));
    const NativePipelineResult result = run_fixture(fixture, 1, &ir);
    const auto counts = candidate_counts(ir.symbols, result.reduced_candidates);
    require(counts.at("A B") == 2);
    require(result.pattern_mining_diagnostics.rows.empty());
  }

  {
    NativeIr ir;
    const ProtectedSequenceFixture fixture =
        load_protected_sequence_fixture(
            fixture_path("f2_hard_replay_unit_boundary"));
    const NativePipelineResult result1 = run_fixture(fixture, 1, &ir);
    const NativePipelineResult result8 = run_fixture(fixture, 8, nullptr);
    const auto counts = candidate_counts(ir.symbols, result1.reduced_candidates);
    require(counts.at("ACLH ACLL") == 2);
    require(counts.at("ACLL ACLT") == 2);
    require(counts.at("ACLH ACLL ACLT") == 2);
    require(counts.find("ACLT ACLH") == counts.end());
    require(counts.find("ACLT ACLH ACLL") == counts.end());
    require(diagnostic_count(result1,
                             CandidateDiagnosticCode::kCrossesNoCrossBoundary) >
            0);
    require(summaries_equal(result1.reduced_candidates,
                            result8.reduced_candidates));
  }

  {
    NativeIr ir;
    const ProtectedSequenceFixture fixture =
        load_protected_sequence_fixture(
            fixture_path("f3_halo_duplicate_ownership"));
    const NativePipelineResult result = run_fixture(fixture, 8, &ir);
    const auto counts = candidate_counts(ir.symbols, result.reduced_candidates);
    require(counts.at("A B C") == 3);
  }

  {
    NativeIr ir;
    const ProtectedSequenceFixture fixture =
        load_protected_sequence_fixture(
            fixture_path("f4_forbidden_enclosing_interval"));
    const NativePipelineResult result = run_fixture(fixture, 4, &ir);
    const auto counts = candidate_counts(ir.symbols, result.reduced_candidates);
    require(counts.at("ACLH ACLL ACLT") == 2);
    require(counts.find("X ACLH ACLL ACLT Y") == counts.end());
    require(diagnostic_count(result,
                             CandidateDiagnosticCode::kEnclosesNoCrossInterval) >
            0);
  }

  {
    NativeIr ir;
    const ProtectedSequenceFixture fixture =
        load_protected_sequence_fixture(fixture_path("f5_ambiguous_interval"));
    const NativePipelineResult result = run_fixture(fixture, 2, &ir);
    const auto counts = candidate_counts(ir.symbols, result.reduced_candidates);
    require(counts.find("A B") == counts.end());
    require(counts.find("B C") == counts.end());
    require(counts.find("A B C") == counts.end());
    require(diagnostic_count(
                result,
                CandidateDiagnosticCode::kAmbiguousIntervalBlocksCandidate) >
            0);
  }

  {
    NativeIr ir;
    const ProtectedSequenceFixture fixture =
        load_protected_sequence_fixture(fixture_path("f6_tie_stability"));
    const NativePipelineResult result1 = run_fixture(fixture, 1, &ir);
    const NativePipelineResult result8 = run_fixture(fixture, 8, nullptr);
    require(summaries_equal(result1.reduced_candidates,
                            result8.reduced_candidates));
  }

  return 0;
}
