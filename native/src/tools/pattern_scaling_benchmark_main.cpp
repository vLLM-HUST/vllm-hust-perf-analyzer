#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "traceloom/core/sha256.h"
#include "traceloom/ir/protected_interval_table.h"
#include "traceloom/ir/token_table.h"
#include "traceloom/pattern/candidate.h"
#include "traceloom/pattern/candidate_reduce.h"
#include "traceloom/pattern/candidate_scan.h"
#include "traceloom/sequence/boundary_index.h"
#include "traceloom/sequence/partition_plan.h"
#include "traceloom/sequence/protected_sequence.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
  std::string mode = "baseline";
  std::size_t tokens = 0;
  std::size_t threads = 0;
  std::size_t partition_tokens = 4096;
  std::size_t halo_tokens = 3;
  std::size_t protected_intervals = 8;
};

std::size_t parse_size(const char* value, const char* name) {
  std::size_t parsed = 0;
  try {
    const std::string text(value);
    std::size_t consumed = 0;
    const unsigned long long raw = std::stoull(text, &consumed);
    if (consumed != text.size() || raw == 0 ||
        raw > std::numeric_limits<std::size_t>::max()) {
      throw std::invalid_argument("range");
    }
    parsed = static_cast<std::size_t>(raw);
  } catch (const std::exception&) {
    throw std::invalid_argument(std::string("invalid ") + name + ": " + value);
  }
  return parsed;
}

Options parse_options(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string argument(argv[index]);
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing value for " + argument);
    }
    const char* value = argv[++index];
    if (argument == "--mode") {
      options.mode = value;
    } else if (argument == "--tokens") {
      options.tokens = parse_size(value, "--tokens");
    } else if (argument == "--threads") {
      options.threads = parse_size(value, "--threads");
    } else if (argument == "--partition-tokens") {
      options.partition_tokens = parse_size(value, "--partition-tokens");
    } else if (argument == "--halo-tokens") {
      options.halo_tokens = parse_size(value, "--halo-tokens");
    } else if (argument == "--protected-intervals") {
      options.protected_intervals =
          parse_size(value, "--protected-intervals");
    } else {
      throw std::invalid_argument("unknown argument: " + argument);
    }
  }
  if (options.tokens < 16 || options.threads == 0) {
    throw std::invalid_argument("--tokens >= 16 and --threads are required");
  }
  if (options.mode != "baseline" && options.mode != "local-reduce") {
    throw std::invalid_argument("--mode must be baseline or local-reduce");
  }
  if (options.tokens >= std::numeric_limits<std::uint32_t>::max()) {
    throw std::invalid_argument("--tokens exceeds the dense typed-id range");
  }
  if (options.halo_tokens < 3) {
    throw std::invalid_argument("--halo-tokens must cover max candidate length 3");
  }
  if (options.protected_intervals * 8 >= options.tokens) {
    throw std::invalid_argument("protected intervals are too dense");
  }
  return options;
}

void update_u64(traceloom::Sha256& digest, std::uint64_t value) {
  std::uint8_t bytes[8];
  for (std::size_t index = 0; index < 8; ++index) {
    bytes[index] = static_cast<std::uint8_t>((value >> (index * 8)) & 0xffu);
  }
  digest.update(bytes, sizeof(bytes));
}

void update_key(traceloom::Sha256& digest,
                const traceloom::CandidateKey& key) {
  update_u64(digest, key.symbols.size());
  for (const traceloom::SymbolId symbol : key.symbols) {
    update_u64(digest, symbol.value());
  }
}

std::string occurrence_digest(
    const std::vector<traceloom::CandidateOccurrence>& rows) {
  traceloom::Sha256 digest;
  update_u64(digest, rows.size());
  for (const traceloom::CandidateOccurrence& row : rows) {
    update_key(digest, row.key);
    update_u64(digest, row.begin);
    update_u64(digest, row.end);
    update_u64(digest, row.partition_id.value());
  }
  return digest.hex_digest();
}

std::string diagnostic_digest(
    const std::vector<traceloom::CandidateDiagnostic>& rows) {
  traceloom::Sha256 digest;
  update_u64(digest, rows.size());
  for (const traceloom::CandidateDiagnostic& row : rows) {
    update_u64(digest, static_cast<std::uint64_t>(row.code));
    update_key(digest, row.key);
    update_u64(digest, row.begin);
    update_u64(digest, row.end);
    update_u64(digest, row.partition_id.value());
    update_u64(digest, row.protected_interval_id.value());
  }
  return digest.hex_digest();
}

std::string summary_digest(
    const std::vector<traceloom::CandidateSummaryRow>& rows) {
  traceloom::Sha256 digest;
  update_u64(digest, rows.size());
  for (const traceloom::CandidateSummaryRow& row : rows) {
    update_key(digest, row.key);
    update_u64(digest, row.occurrence_count);
    update_u64(digest, row.first_begin);
  }
  return digest.hex_digest();
}

double elapsed_ms(const Clock::time_point& begin,
                  const Clock::time_point& end) {
  return std::chrono::duration<double, std::milli>(end - begin).count();
}

traceloom::SymbolId symbol_for(std::size_t index) {
  static constexpr std::uint32_t kMotif[] = {
      0, 1, 2, 3, 0, 1, 4, 5, 0, 1, 2, 3, 6, 7, 4, 5,
  };
  const std::uint32_t phase =
      static_cast<std::uint32_t>((index / 4096u) % 8u) * 16u;
  return traceloom::make_id<traceloom::SymbolId>(
      phase + kMotif[index % (sizeof(kMotif) / sizeof(kMotif[0]))]);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = parse_options(argc, argv);

    traceloom::TokenTable tokens;
    for (std::size_t index = 0; index < options.tokens; ++index) {
      tokens.append(
          traceloom::make_id<traceloom::AnchorId>(
              static_cast<traceloom::AnchorId::value_type>(index)),
          symbol_for(index), 0, static_cast<std::uint32_t>(index),
          static_cast<std::int64_t>(index * 2u),
          static_cast<std::int64_t>(index * 2u + 1u));
    }
    const traceloom::ProtectedSequence sequence =
        traceloom::ProtectedSequence::from_token_table(tokens);

    traceloom::ProtectedIntervalTable intervals;
    for (std::size_t index = 0; index < options.protected_intervals; ++index) {
      const std::size_t first =
          ((index + 1u) * options.tokens) / (options.protected_intervals + 1u);
      const std::size_t last = first + 3u;
      intervals.append(
          traceloom::ProtectedIntervalKind::kGraphReplayUnit,
          traceloom::BoundaryPolicy::kNoCross, tokens.rows()[first].id,
          tokens.rows()[last].id, tokens.rows()[first].anchor_id,
          tokens.rows()[last].anchor_id, traceloom::SourceRefId::invalid());
    }
    const traceloom::BoundaryIndex boundaries =
        traceloom::BoundaryIndex::build(sequence, intervals);
    const traceloom::PartitionPlan plan = traceloom::PartitionPlan::build(
        sequence.size(), traceloom::PartitionPlanConfig{
                             options.partition_tokens, options.halo_tokens});

    if (options.mode == "local-reduce") {
      const Clock::time_point aggregate_begin = Clock::now();
      const traceloom::CandidateAggregateResult aggregate =
          traceloom::scan_and_reduce_candidate_partitions(
              sequence, boundaries, plan,
              traceloom::CandidateScanConfig{2, 3}, options.threads);
      const Clock::time_point aggregate_end = Clock::now();
      const std::string diagnostics_sha256 =
          diagnostic_digest(aggregate.diagnostics);
      const std::string reduced_sha256 = summary_digest(aggregate.summaries);
      std::cout
          << "{\n"
          << "  \"schema_version\": "
             "\"traceloom-pattern-aggregate-sample-v1\",\n"
          << "  \"mode\": \"local-reduce\",\n"
          << "  \"tokens\": " << options.tokens << ",\n"
          << "  \"threads\": " << options.threads << ",\n"
          << "  \"partition_tokens\": " << options.partition_tokens
          << ",\n"
          << "  \"halo_tokens\": " << options.halo_tokens << ",\n"
          << "  \"partitions\": " << plan.size() << ",\n"
          << "  \"protected_intervals\": "
          << options.protected_intervals << ",\n"
          << "  \"candidate_occurrences\": "
          << aggregate.occurrence_count << ",\n"
          << "  \"candidate_diagnostics\": "
          << aggregate.diagnostics.size() << ",\n"
          << "  \"reduced_candidates\": " << aggregate.summaries.size()
          << ",\n"
          << "  \"map_reduce_ms\": "
          << elapsed_ms(aggregate_begin, aggregate_end) << ",\n"
          << "  \"diagnostics_sha256\": \"" << diagnostics_sha256
          << "\",\n"
          << "  \"reduced_sha256\": \"" << reduced_sha256 << "\"\n"
          << "}\n";
      return EXIT_SUCCESS;
    }

    const Clock::time_point scan_begin = Clock::now();
    traceloom::CandidateScanResult scan =
        traceloom::scan_candidate_partitions_with_diagnostics(
            sequence, boundaries, plan, traceloom::CandidateScanConfig{2, 3},
            options.threads);
    const Clock::time_point scan_end = Clock::now();

    const std::size_t occurrence_count = scan.occurrences.size();
    const std::size_t diagnostic_count = scan.diagnostics.size();
    const std::string occurrences_sha256 = occurrence_digest(scan.occurrences);
    const std::string diagnostics_sha256 = diagnostic_digest(scan.diagnostics);

    const Clock::time_point reduce_begin = Clock::now();
    const std::vector<traceloom::CandidateSummaryRow> summary =
        traceloom::reduce_candidates(std::move(scan.occurrences));
    const Clock::time_point reduce_end = Clock::now();
    const std::string reduced_sha256 = summary_digest(summary);

    const double scan_ms = elapsed_ms(scan_begin, scan_end);
    const double reduce_ms = elapsed_ms(reduce_begin, reduce_end);
    std::cout << "{\n"
              << "  \"schema_version\": \"traceloom-pattern-sample-v1\",\n"
              << "  \"tokens\": " << options.tokens << ",\n"
              << "  \"threads\": " << options.threads << ",\n"
              << "  \"partition_tokens\": " << options.partition_tokens
              << ",\n"
              << "  \"halo_tokens\": " << options.halo_tokens << ",\n"
              << "  \"partitions\": " << plan.size() << ",\n"
              << "  \"protected_intervals\": "
              << options.protected_intervals << ",\n"
              << "  \"candidate_occurrences\": " << occurrence_count
              << ",\n"
              << "  \"candidate_diagnostics\": " << diagnostic_count
              << ",\n"
              << "  \"reduced_candidates\": " << summary.size() << ",\n"
              << "  \"scan_ms\": " << scan_ms << ",\n"
              << "  \"reduce_ms\": " << reduce_ms << ",\n"
              << "  \"scan_plus_reduce_ms\": " << (scan_ms + reduce_ms)
              << ",\n"
              << "  \"occurrences_sha256\": \"" << occurrences_sha256
              << "\",\n"
              << "  \"diagnostics_sha256\": \"" << diagnostics_sha256
              << "\",\n"
              << "  \"reduced_sha256\": \"" << reduced_sha256 << "\"\n"
              << "}\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return EXIT_FAILURE;
  }
}
