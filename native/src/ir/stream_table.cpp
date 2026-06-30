#include "traceloom/ir/stream_table.h"

#include <stdexcept>

namespace traceloom {

StreamId StreamTable::append(SourceRefId source_ref_id,
                             std::uint32_t device_id,
                             std::uint64_t raw_stream_id) {
  const auto id = checked_next_id<StreamId>(rows_.size());
  rows_.push_back(StreamRow{id, source_ref_id, device_id, raw_stream_id});
  return id;
}

const StreamRow& StreamTable::row(StreamId id) const {
  if (!id.valid() || id.value() >= rows_.size()) {
    throw std::out_of_range("StreamId is out of range");
  }
  return rows_[id.value()];
}

}  // namespace traceloom
