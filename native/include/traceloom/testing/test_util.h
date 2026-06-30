#pragma once

#include <cstdlib>
#include <iostream>

namespace traceloom::testing {

inline void require(bool condition, const char* message = "require failed") {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

}  // namespace traceloom::testing
