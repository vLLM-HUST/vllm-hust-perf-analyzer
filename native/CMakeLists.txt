cmake_minimum_required(VERSION 3.16)

project(traceloom_native LANGUAGES CXX)

option(TRACELOOM_NATIVE_BUILD_TESTS "Build TraceLoom native tests" ON)
option(
  TRACELOOM_NATIVE_ENABLE_ASCEND_SQLITE
  "Build optional Ascend SQLite source adapter"
  ON
)
set(TRACELOOM_NATIVE_ASCEND_SQLITE_AVAILABLE OFF)

add_library(traceloom_native_core
  src/analysis/flat_anchor_builder.cpp
  src/analysis/native_pipeline.cpp
  src/core/diagnostics.cpp
  src/core/string_table.cpp
  src/cost/cost_summary_lite.cpp
  src/ir/anchor_table.cpp
  src/ir/capture_slot_table.cpp
  src/ir/communication_op_table.cpp
  src/ir/graph_template_table.cpp
  src/ir/protected_interval_table.cpp
  src/ir/replay_unit_table.cpp
  src/ir/source_ref_table.cpp
  src/ir/stream_table.cpp
  src/ir/task_table.cpp
  src/ir/token_table.cpp
  src/ir/trace_event_table.cpp
  src/pattern/candidate_reduce.cpp
  src/pattern/candidate_scan.cpp
  src/pattern/pattern_candidate_table.cpp
  src/pattern/pattern_candidate_summary.cpp
  src/runtime/thread_pool.cpp
  src/sequence/boundary_index.cpp
  src/sequence/partition_plan.cpp
  src/sequence/protected_sequence.cpp
)

target_include_directories(traceloom_native_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(traceloom_native_core PUBLIC cxx_std_17)

if(MSVC)
  target_compile_options(traceloom_native_core PRIVATE /W4)
else()
  target_compile_options(traceloom_native_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(traceloom_native_adapters
  src/adapters/fixture_adapter.cpp
)

target_link_libraries(traceloom_native_adapters PUBLIC traceloom_native_core)

if(TRACELOOM_NATIVE_ENABLE_ASCEND_SQLITE)
  find_package(SQLite3 QUIET)
  if(SQLite3_FOUND)
    target_sources(traceloom_native_adapters
      PRIVATE
        src/adapters/ascend_sqlite_adapter.cpp
    )
    if(TARGET SQLite::SQLite3)
      target_link_libraries(traceloom_native_adapters PRIVATE SQLite::SQLite3)
    else()
      target_include_directories(
        traceloom_native_adapters PRIVATE ${SQLite3_INCLUDE_DIRS}
      )
      target_link_libraries(
        traceloom_native_adapters PRIVATE ${SQLite3_LIBRARIES}
      )
    endif()
    target_compile_definitions(
      traceloom_native_adapters
      PUBLIC TRACELOOM_NATIVE_HAS_ASCEND_SQLITE=1
    )
    set(TRACELOOM_NATIVE_ASCEND_SQLITE_AVAILABLE ON)
  else()
    message(STATUS "SQLite3 not found; Ascend SQLite adapter is disabled")
  endif()
endif()

if(MSVC)
  target_compile_options(traceloom_native_adapters PRIVATE /W4)
else()
  target_compile_options(traceloom_native_adapters PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(TRACELOOM_NATIVE_ASCEND_SQLITE_AVAILABLE)
  add_executable(traceloom-native-ascend-sqlite-inventory
    src/tools/ascend_sqlite_inventory_main.cpp
  )
  target_link_libraries(
    traceloom-native-ascend-sqlite-inventory PRIVATE traceloom_native_adapters
  )
endif()

if(TRACELOOM_NATIVE_BUILD_TESTS)
  enable_testing()

  add_executable(traceloom_native_id_tests
    tests/core/id_tests.cpp
  )
  target_link_libraries(traceloom_native_id_tests PRIVATE traceloom_native_core)
  add_test(NAME traceloom_native_id_tests COMMAND traceloom_native_id_tests)

  add_executable(traceloom_native_cost_summary_tests
    tests/cost/cost_summary_lite_tests.cpp
  )
  target_link_libraries(
    traceloom_native_cost_summary_tests PRIVATE traceloom_native_core
  )
  add_test(
    NAME traceloom_native_cost_summary_tests
    COMMAND traceloom_native_cost_summary_tests
  )

  add_executable(traceloom_native_ir_contract_tests
    tests/ir/native_ir_contract_tests.cpp
  )
  target_link_libraries(traceloom_native_ir_contract_tests PRIVATE traceloom_native_core)
  add_test(
    NAME traceloom_native_ir_contract_tests
    COMMAND traceloom_native_ir_contract_tests
  )

  add_executable(traceloom_native_sequence_contract_tests
    tests/sequence/sequence_contract_tests.cpp
  )
  target_link_libraries(
    traceloom_native_sequence_contract_tests PRIVATE traceloom_native_core
  )
  add_test(
    NAME traceloom_native_sequence_contract_tests
    COMMAND traceloom_native_sequence_contract_tests
  )

  add_executable(traceloom_native_thread_pool_tests
    tests/runtime/thread_pool_tests.cpp
  )
  target_link_libraries(
    traceloom_native_thread_pool_tests PRIVATE traceloom_native_core
  )
  add_test(
    NAME traceloom_native_thread_pool_tests
    COMMAND traceloom_native_thread_pool_tests
  )

  add_executable(traceloom_native_candidate_scan_tests
    tests/pattern/candidate_scan_tests.cpp
  )
  target_link_libraries(
    traceloom_native_candidate_scan_tests PRIVATE traceloom_native_core
  )
  add_test(
    NAME traceloom_native_candidate_scan_tests
    COMMAND traceloom_native_candidate_scan_tests
  )

  add_executable(traceloom_native_flat_anchor_builder_tests
    tests/analysis/flat_anchor_builder_tests.cpp
  )
  target_link_libraries(
    traceloom_native_flat_anchor_builder_tests PRIVATE traceloom_native_core
  )
  add_test(
    NAME traceloom_native_flat_anchor_builder_tests
    COMMAND traceloom_native_flat_anchor_builder_tests
  )

  add_executable(traceloom_native_pipeline_tests
    tests/analysis/native_pipeline_tests.cpp
  )
  target_link_libraries(
    traceloom_native_pipeline_tests PRIVATE traceloom_native_core
  )
  add_test(
    NAME traceloom_native_pipeline_tests
    COMMAND traceloom_native_pipeline_tests
  )

  add_executable(traceloom_native_fixture_adapter_tests
    tests/adapters/fixture_adapter_tests.cpp
  )
  target_link_libraries(
    traceloom_native_fixture_adapter_tests PRIVATE traceloom_native_adapters
  )
  add_test(
    NAME traceloom_native_fixture_adapter_tests
    COMMAND traceloom_native_fixture_adapter_tests
  )

  if(TRACELOOM_NATIVE_ASCEND_SQLITE_AVAILABLE)
    add_executable(traceloom_native_ascend_sqlite_adapter_tests
      tests/adapters/ascend_sqlite_adapter_tests.cpp
    )
    target_link_libraries(
      traceloom_native_ascend_sqlite_adapter_tests PRIVATE
      traceloom_native_adapters
    )
    if(TARGET SQLite::SQLite3)
      target_link_libraries(
        traceloom_native_ascend_sqlite_adapter_tests PRIVATE SQLite::SQLite3
      )
    else()
      target_include_directories(
        traceloom_native_ascend_sqlite_adapter_tests
        PRIVATE ${SQLite3_INCLUDE_DIRS}
      )
      target_link_libraries(
        traceloom_native_ascend_sqlite_adapter_tests
        PRIVATE ${SQLite3_LIBRARIES}
      )
    endif()
    add_test(
      NAME traceloom_native_ascend_sqlite_adapter_tests
      COMMAND traceloom_native_ascend_sqlite_adapter_tests
    )
  endif()

  add_test(
    NAME traceloom_native_include_boundary_tests
    COMMAND ${CMAKE_COMMAND}
      -DTRACELOOM_NATIVE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/boundary/include_boundary_tests.cmake
  )
endif()
