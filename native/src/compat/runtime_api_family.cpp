#include "runtime_api_family.h"

#include <algorithm>
#include <cctype>

namespace traceloom::compat::detail {
namespace {

bool contains(const std::string& value, const char* needle) {
  return value.find(needle) != std::string::npos;
}

}  // namespace

std::string public_runtime_api_family(const std::string& api_name) {
  std::string name = api_name;
  std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  if (!(name.rfind("acl", 0) == 0 || name.rfind("cuda", 0) == 0 ||
        name.rfind("hip", 0) == 0)) {
    return {};
  }
  if (contains(name, "wait")) return "wait";
  if (contains(name, "synchronize")) return "synchronize";
  if (contains(name, "query")) return "query";
  if (contains(name, "eventrecord") || contains(name, "recordevent")) {
    return "event_record";
  }
  if (contains(name, "eventcreate") || contains(name, "createevent") ||
      contains(name, "eventdestroy") || contains(name, "destroyevent")) {
    return "event_lifecycle";
  }
  if (contains(name, "graphlaunch") ||
      contains(name, "aclmdlriexecuteasync")) {
    return "graph_launch";
  }
  if (contains(name, "launch")) return "launch";
  if (contains(name, "memcpy") || contains(name, "memset") ||
      contains(name, "inplacecopy")) {
    return "memory";
  }
  if (contains(name, "capture") || contains(name, "graph")) {
    return "graph_control";
  }
  return "other";
}

}  // namespace traceloom::compat::detail
