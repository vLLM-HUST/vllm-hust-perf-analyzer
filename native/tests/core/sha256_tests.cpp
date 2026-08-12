#include "traceloom/core/sha256.h"
#include "traceloom/testing/test_util.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

int main() {
  using namespace traceloom;
  using traceloom::testing::require;

  // FIPS 180-4 standard test vectors.
  require(sha256_hex("") ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "sha256 empty string vector");
  require(sha256_hex("abc") ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "sha256 abc vector");
  require(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "sha256 multi-block vector");

  // Streaming chunks must equal one-shot hashing.
  const std::string sample =
      "the quick brown fox jumps over the lazy dog and keeps running";
  {
    Sha256 one_shot;
    one_shot.update(sample);
    const std::string expected = one_shot.hex_digest();
    Sha256 chunked;
    for (std::size_t offset = 0; offset < sample.size(); offset += 7) {
      chunked.update(sample.data() + offset,
                     std::min<std::size_t>(7, sample.size() - offset));
    }
    require(chunked.hex_digest() == expected,
            "sha256 streaming chunk equivalence");
  }

  // File hashing: stable for identical bytes, changes with content.
  const std::filesystem::path file =
      std::filesystem::temp_directory_path() / "traceloom-sha256-test.txt";
  {
    std::ofstream out(file, std::ios::binary);
    out << "analysis-artifact-v1\n";
  }
  const std::string first = sha256_file_hex(file.string());
  require(first == sha256_file_hex(file.string()),
          "sha256 file hash stable for identical bytes");
  {
    std::ofstream out(file, std::ios::binary);
    out << "analysis-artifact-v2\n";
  }
  require(sha256_file_hex(file.string()) != first,
          "sha256 file hash changes with content");
  std::filesystem::remove(file);
  return 0;
}
