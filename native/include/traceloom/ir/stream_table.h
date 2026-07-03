#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "traceloom/core/ids.h"

namespace traceloom {

struct StreamRow {
  StreamId id;
  SourceRefId source_ref_id;
  std::uint32_t device_id = 0;
  std::uint64_t raw_stream_id = 0;
};

class StreamTable {
 public:
  StreamId append(SourceRefId source_ref_id,
                  std::uint32_t device_id,
                  std::uint64_t raw_stream_id);

  std::size_t size() const noexcept { return rows_.size(); }
  bool empty() const noexcept { return rows_.empty(); }
  void reserve(std::size_t count) { rows_.reserve(count); }
  const StreamRow& row(StreamId id) const;
  const std::vector<StreamRow>& rows() const noexcept { return rows_; }

 private:
  std::vector<StreamRow> rows_;
};

}  // namespace traceloom
