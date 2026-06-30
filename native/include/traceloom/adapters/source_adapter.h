#pragma once

#include "traceloom/ir/native_ir.h"

namespace traceloom {

class SourceAdapter {
 public:
  virtual ~SourceAdapter() = default;
  virtual NativeIr load() const = 0;
};

}  // namespace traceloom
