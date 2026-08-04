#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

enum class CaptureAssociationPolicy : std::uint8_t {
  kModelGroupOnly = 0,
  kCaptureOrdinalAligned = 1,
};

struct CapturedGraphInstanceRow {
  CapturedGraphInstanceId id;
  SourceRefId source_ref_id;
  std::uint64_t first_source_row_id = 0;
  std::uint32_t device_id = 0;
  std::int64_t raw_model_id = -1;
  std::int64_t raw_capture_timestamp = -1;
  std::int64_t capture_ordinal = -1;
  GraphSlotTemplateId slot_template_id;
  std::uint32_t model_stream_count = 0;
  CaptureAssociationPolicy association_policy =
      CaptureAssociationPolicy::kModelGroupOnly;
};

class CapturedGraphInstanceTable {
 public:
  CapturedGraphInstanceId append(
      SourceRefId source_ref_id,
      std::uint64_t first_source_row_id,
      std::uint32_t device_id,
      std::int64_t raw_model_id,
      std::int64_t raw_capture_timestamp,
      std::int64_t capture_ordinal,
      GraphSlotTemplateId slot_template_id,
      std::uint32_t model_stream_count,
      CaptureAssociationPolicy association_policy);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const CapturedGraphInstanceRow& row(CapturedGraphInstanceId id) const;
  const std::vector<CapturedGraphInstanceRow>& rows() const noexcept {
    return rows_;
  }

 private:
  std::vector<CapturedGraphInstanceRow> rows_;
};

struct CapturedGraphStreamRow {
  CapturedGraphStreamId id;
  CapturedGraphInstanceId captured_graph_instance_id;
  SourceRefId source_ref_id;
  std::uint64_t source_row_id = 0;
  std::int64_t raw_original_stream_id = -1;
  std::uint64_t raw_model_stream_id = 0;
};

class CapturedGraphStreamTable {
 public:
  CapturedGraphStreamId append(
      CapturedGraphInstanceId captured_graph_instance_id,
      SourceRefId source_ref_id,
      std::uint64_t source_row_id,
      std::int64_t raw_original_stream_id,
      std::uint64_t raw_model_stream_id);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  const CapturedGraphStreamRow& row(CapturedGraphStreamId id) const;
  const std::vector<CapturedGraphStreamRow>& rows() const noexcept {
    return rows_;
  }

 private:
  std::vector<CapturedGraphStreamRow> rows_;
};

}  // namespace traceloom
