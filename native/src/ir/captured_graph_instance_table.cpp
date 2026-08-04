#include "traceloom/ir/captured_graph_instance_table.h"

#include <stdexcept>

namespace traceloom {

CapturedGraphInstanceId CapturedGraphInstanceTable::append(
    SourceRefId source_ref_id,
    std::uint64_t first_source_row_id,
    std::uint32_t device_id,
    std::int64_t raw_model_id,
    std::int64_t raw_capture_timestamp,
    std::int64_t capture_ordinal,
    GraphSlotTemplateId slot_template_id,
    std::uint32_t model_stream_count,
    CaptureAssociationPolicy association_policy) {
  const auto id = checked_next_id<CapturedGraphInstanceId>(rows_.size());
  rows_.push_back(CapturedGraphInstanceRow{
      id, source_ref_id, first_source_row_id, device_id, raw_model_id,
      raw_capture_timestamp, capture_ordinal, slot_template_id,
      model_stream_count, association_policy});
  return id;
}

const CapturedGraphInstanceRow& CapturedGraphInstanceTable::row(
    CapturedGraphInstanceId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("CapturedGraphInstanceId is out of range");
  }
  return rows_[id.value()];
}

CapturedGraphStreamId CapturedGraphStreamTable::append(
    CapturedGraphInstanceId captured_graph_instance_id,
    SourceRefId source_ref_id,
    std::uint64_t source_row_id,
    std::int64_t raw_original_stream_id,
    std::uint64_t raw_model_stream_id) {
  const auto id = checked_next_id<CapturedGraphStreamId>(rows_.size());
  rows_.push_back(CapturedGraphStreamRow{
      id, captured_graph_instance_id, source_ref_id, source_row_id,
      raw_original_stream_id, raw_model_stream_id});
  return id;
}

const CapturedGraphStreamRow& CapturedGraphStreamTable::row(
    CapturedGraphStreamId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("CapturedGraphStreamId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
