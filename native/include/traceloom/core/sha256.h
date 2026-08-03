#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace traceloom {

// Streaming SHA-256 (FIPS 180-4) with no external dependencies.
// Digest is emitted as lowercase hexadecimal, matching the idle evidence
// contract run_id / ruleset-sha256 conventions.
class Sha256 {
 public:
  Sha256();

  Sha256& update(const void* data, std::size_t size);
  Sha256& update(const std::string& data);

  // Finalizes the digest. Safe to call once; later updates are ignored.
  std::string hex_digest();

 private:
  void transform(const std::uint8_t block[64]);

  std::uint32_t state_[8];
  std::uint8_t buffer_[64];
  std::size_t buffer_len_ = 0;
  std::uint64_t total_bytes_ = 0;
  bool finalized_ = false;
};

std::string sha256_hex(const std::string& data);
std::string sha256_file_hex(const std::string& path);

}  // namespace traceloom
