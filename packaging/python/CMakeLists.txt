cmake_minimum_required(VERSION 3.16)
project(traceloom_python LANGUAGES CXX)

# Reuse the production implementation; do not install development tools or tests.
set(TRACELOOM_NATIVE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TRACELOOM_NATIVE_ENABLE_SQLITE_ADAPTERS ON CACHE BOOL "" FORCE)
add_subdirectory(../../native native EXCLUDE_FROM_ALL)
if(NOT TARGET traceloom)
  message(FATAL_ERROR "TraceLoom wheels require SQLite development libraries")
endif()
install(TARGETS traceloom RUNTIME DESTINATION traceloom/_native/bin)
install(FILES
  ../../native/data/default_signal_classification_rules.yaml
  ../../native/data/default_structural_symbol_rules.yaml
  ../../native/data/default_event_reconciliation_rules.yaml
  DESTINATION traceloom/_native/share/traceloom)
install(FILES ../../configs/deepseekv4.yaml ../../configs/scheduler-step.yaml
  DESTINATION traceloom/_native/share/traceloom/models)

install(FILES THIRD_PARTY_LICENSES.txt DESTINATION traceloom/_native)
