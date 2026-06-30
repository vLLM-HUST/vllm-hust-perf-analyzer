#pragma once

#include <string>
#include <utility>

namespace traceloom {

enum class StatusCode {
  kOk,
  kInvalidArgument,
  kOutOfRange,
  kInternal,
};

class Status {
 public:
  Status() = default;
  Status(StatusCode code, std::string message)
      : code_(code), message_(std::move(message)) {}

  static Status ok() { return Status(); }

  bool ok_status() const noexcept { return code_ == StatusCode::kOk; }
  StatusCode code() const noexcept { return code_; }
  const std::string& message() const noexcept { return message_; }

 private:
  StatusCode code_ = StatusCode::kOk;
  std::string message_;
};

}  // namespace traceloom
