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
  src/pattern/candidate.cpp
  src/pattern/candidate_reduce.cpp
  src/pattern/candidate_scan.cpp
  src/pattern/grammar_apply.cpp
  src/pattern/grammar_commit_plan.cpp
  src/pattern/grammar_engine.cpp
  src/pattern/grammar_modes.cpp
  src/pattern/grammar_round.cpp
  src/pattern/grammar_snapshot.cpp
  src/pattern/grammar_state.cpp
  src/pattern/pattern_candidate_table.cpp
  src/pattern/pattern_candidate_summary.cpp
  src/report/anchor_graph_child_cost.cpp
  src/report/anchor_internal_cost_breakdown.cpp
  src/report/report_tree.cpp
  src/report/report_tree_builder.cpp
  src/report/report_cost_tree.cpp
  src/report/report_tree_cost_handoff.cpp
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
  src/adapters/aclgraph_fixture_adapter.cpp
  src/adapters/aclgraph_fixture_reader.cpp
  src/adapters/fixture_adapter.cpp
  src/adapters/protected_sequence_fixture_reader.cpp
)

target_link_libraries(traceloom_native_adapters PUBLIC traceloom_native_core)

add_library(traceloom_native_materialize
  src/compat/anchor_cost_breakdown_rows.cpp
  src/compat/schema.cpp
  src/materialize/grammar_debug_json.cpp
  src/materialize/native_result_json.cpp
)

target_link_libraries(traceloom_native_materialize PUBLIC traceloom_native_core)

if(MSVC)
  target_compile_options(traceloom_native_materialize PRIVATE /W4)
else()
  target_compile_options(
    traceloom_native_materialize PRIVATE -Wall -Wextra -Wpedantic
  )
endif()

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

  add_executable(traceloom-native-analyze-db
    src/tools/analyze_db_main.cpp
  )
  target_link_libraries(
    traceloom-native-analyze-db PRIVATE
    traceloom_native_adapters
    traceloom_native_materialize
  )
endif()

add_executable(traceloom-native-analyze-fixture
  src/tools/analyze_fixture_main.cpp
)
target_link_libraries(
  traceloom-native-analyze-fixture PRIVATE
  traceloom_native_adapters
  traceloom_native_materialize
)

add_executable(traceloom-native-analyze-aclgraph-fixture
  src/tools/analyze_aclgraph_fixture_main.cpp
)
target_link_libraries(
  traceloom-native-analyze-aclgraph-fixture PRIVATE
  traceloom_native_adapters
  traceloom_native_materialize
)

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

  add_executable(traceloom_native_grammar_state_tests
    tests/pattern/grammar_state_tests.cpp
  )
  target_link_libraries(
    traceloom_native_grammar_state_tests PRIVATE
    traceloom_native_adapters
  )
  add_test(
    NAME traceloom_native_grammar_state_tests
    COMMAND traceloom_native_grammar_state_tests
  )

  add_executable(traceloom_native_grammar_snapshot_tests
    tests/pattern/grammar_snapshot_tests.cpp
  )
  target_link_libraries(
    traceloom_native_grammar_snapshot_tests PRIVATE
    traceloom_native_adapters
  )
  add_test(
    NAME traceloom_native_grammar_snapshot_tests
    COMMAND traceloom_native_grammar_snapshot_tests
  )

  add_executable(traceloom_native_grammar_round_tests
    tests/pattern/grammar_round_tests.cpp
  )
  target_link_libraries(
    traceloom_native_grammar_round_tests PRIVATE
    traceloom_native_adapters
  )
  add_test(
    NAME traceloom_native_grammar_round_tests
    COMMAND traceloom_native_grammar_round_tests
  )

  add_executable(traceloom_native_grammar_commit_plan_tests
    tests/pattern/grammar_commit_plan_tests.cpp
  )
  target_link_libraries(
    traceloom_native_grammar_commit_plan_tests PRIVATE
    traceloom_native_adapters
  )
  add_test(
    NAME traceloom_native_grammar_commit_plan_tests
    COMMAND traceloom_native_grammar_commit_plan_tests
  )

  add_executable(traceloom_native_grammar_apply_tests
    tests/pattern/grammar_apply_tests.cpp
  )
  target_link_libraries(
    traceloom_native_grammar_apply_tests PRIVATE
    traceloom_native_adapters
  )
  add_test(
    NAME traceloom_native_grammar_apply_tests
    COMMAND traceloom_native_grammar_apply_tests
  )

  add_executable(traceloom_native_grammar_engine_tests
    tests/pattern/grammar_engine_tests.cpp
  )
  target_link_libraries(
    traceloom_native_grammar_engine_tests PRIVATE
    traceloom_native_adapters
  )
  add_test(
    NAME traceloom_native_grammar_engine_tests
    COMMAND traceloom_native_grammar_engine_tests
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

  add_executable(traceloom_native_report_tree_builder_tests
    tests/report/report_tree_builder_tests.cpp
  )
  target_link_libraries(
    traceloom_native_report_tree_builder_tests PRIVATE traceloom_native_core
  )
  add_test(
    NAME traceloom_native_report_tree_builder_tests
    COMMAND traceloom_native_report_tree_builder_tests
  )

  add_executable(traceloom_native_report_tree_visibility_tests
    tests/report/report_tree_visibility_tests.cpp
  )
  target_link_libraries(
    traceloom_native_report_tree_visibility_tests PRIVATE traceloom_native_core
  )
  add_test(
    NAME traceloom_native_report_tree_visibility_tests
    COMMAND traceloom_native_report_tree_visibility_tests
  )

  add_executable(traceloom_native_report_tree_graph_boundary_tests
    tests/report/report_tree_graph_boundary_tests.cpp
  )
  target_link_libraries(
    traceloom_native_report_tree_graph_boundary_tests PRIVATE
    traceloom_native_core
  )
  add_test(
    NAME traceloom_native_report_tree_graph_boundary_tests
    COMMAND traceloom_native_report_tree_graph_boundary_tests
  )

  add_executable(traceloom_native_report_tree_cost_handoff_tests
    tests/report/report_tree_cost_handoff_tests.cpp
  )
  target_link_libraries(
    traceloom_native_report_tree_cost_handoff_tests PRIVATE
    traceloom_native_core
  )
  add_test(
    NAME traceloom_native_report_tree_cost_handoff_tests
    COMMAND traceloom_native_report_tree_cost_handoff_tests
  )

  add_executable(traceloom_native_report_cost_tree_tests
    tests/report/report_cost_tree_tests.cpp
  )
  target_link_libraries(
    traceloom_native_report_cost_tree_tests PRIVATE traceloom_native_core
  )
  add_test(
    NAME traceloom_native_report_cost_tree_tests
    COMMAND traceloom_native_report_cost_tree_tests
  )

  add_executable(traceloom_native_anchor_internal_cost_breakdown_tests
    tests/report/anchor_internal_cost_breakdown_tests.cpp
  )
  target_link_libraries(
    traceloom_native_anchor_internal_cost_breakdown_tests PRIVATE
    traceloom_native_core
  )
  add_test(
    NAME traceloom_native_anchor_internal_cost_breakdown_tests
    COMMAND traceloom_native_anchor_internal_cost_breakdown_tests
  )

  add_executable(traceloom_native_anchor_graph_child_cost_tests
    tests/report/anchor_graph_child_cost_tests.cpp
  )
  target_link_libraries(
    traceloom_native_anchor_graph_child_cost_tests PRIVATE traceloom_native_core
  )
  add_test(
    NAME traceloom_native_anchor_graph_child_cost_tests
    COMMAND traceloom_native_anchor_graph_child_cost_tests
  )

  add_executable(traceloom_native_result_json_tests
    tests/materialize/native_result_json_tests.cpp
  )
  target_link_libraries(
    traceloom_native_result_json_tests PRIVATE
    traceloom_native_adapters
    traceloom_native_materialize
  )
  add_test(
    NAME traceloom_native_result_json_tests
    COMMAND traceloom_native_result_json_tests
  )

  add_executable(traceloom_native_anchor_cost_breakdown_rows_tests
    tests/compat/anchor_cost_breakdown_rows_tests.cpp
  )
  target_link_libraries(
    traceloom_native_anchor_cost_breakdown_rows_tests PRIVATE
    traceloom_native_materialize
  )
  add_test(
    NAME traceloom_native_anchor_cost_breakdown_rows_tests
    COMMAND traceloom_native_anchor_cost_breakdown_rows_tests
  )

  add_executable(traceloom_native_grammar_debug_json_tests
    tests/materialize/grammar_debug_json_tests.cpp
  )
  target_link_libraries(
    traceloom_native_grammar_debug_json_tests PRIVATE
    traceloom_native_adapters
    traceloom_native_materialize
  )
  add_test(
    NAME traceloom_native_grammar_debug_json_tests
    COMMAND traceloom_native_grammar_debug_json_tests
  )

  add_test(
    NAME traceloom_native_analyze_fixture_grammar_debug_cli
    COMMAND traceloom-native-analyze-fixture
      --fixture
      ${CMAKE_CURRENT_SOURCE_DIR}/../../drafts/refactor/80_tests_fixtures/fixtures/protected_sequence/f1_tiny_repeated_pair.json
      --threads 2
      --out ${CMAKE_CURRENT_BINARY_DIR}/fixture_cli_result.json
      --grammar-debug-out -
  )
  set_tests_properties(
    traceloom_native_analyze_fixture_grammar_debug_cli
    PROPERTIES PASS_REGULAR_EXPRESSION "native_grammar_debug_v1"
  )

  add_test(
    NAME traceloom_native_analyze_fixture_stdout_conflict
    COMMAND traceloom-native-analyze-fixture
      --fixture
      ${CMAKE_CURRENT_SOURCE_DIR}/../../drafts/refactor/80_tests_fixtures/fixtures/protected_sequence/f1_tiny_repeated_pair.json
      --out -
      --grammar-debug-out -
  )
  set_tests_properties(
    traceloom_native_analyze_fixture_stdout_conflict
    PROPERTIES
      WILL_FAIL TRUE
  )

  add_test(
    NAME traceloom_native_analyze_aclgraph_fixture_cli
    COMMAND traceloom-native-analyze-aclgraph-fixture
      --fixture
      ${CMAKE_CURRENT_SOURCE_DIR}/../../drafts/refactor/80_tests_fixtures/fixtures/aclgraph/aclgraph_python_minimal_assets.json
      --threads 2
      --out -
  )
  set_tests_properties(
    traceloom_native_analyze_aclgraph_fixture_cli
    PROPERTIES PASS_REGULAR_EXPRESSION "anchor_internal_cost_breakdown"
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

  add_executable(traceloom_native_aclgraph_fixture_reader_tests
    tests/adapters/aclgraph_fixture_reader_tests.cpp
  )
  target_compile_definitions(
    traceloom_native_aclgraph_fixture_reader_tests PRIVATE
    TRACELOOM_WORKSPACE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../.."
  )
  target_link_libraries(
    traceloom_native_aclgraph_fixture_reader_tests PRIVATE
    traceloom_native_adapters
  )
  add_test(
    NAME traceloom_native_aclgraph_fixture_reader_tests
    COMMAND traceloom_native_aclgraph_fixture_reader_tests
  )

  add_executable(traceloom_native_aclgraph_fixture_adapter_tests
    tests/adapters/aclgraph_fixture_adapter_tests.cpp
  )
  target_compile_definitions(
    traceloom_native_aclgraph_fixture_adapter_tests PRIVATE
    TRACELOOM_WORKSPACE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../.."
  )
  target_link_libraries(
    traceloom_native_aclgraph_fixture_adapter_tests PRIVATE
    traceloom_native_adapters
  )
  add_test(
    NAME traceloom_native_aclgraph_fixture_adapter_tests
    COMMAND traceloom_native_aclgraph_fixture_adapter_tests
  )

  add_executable(traceloom_native_protected_sequence_fixture_reader_tests
    tests/adapters/protected_sequence_fixture_reader_tests.cpp
  )
  target_compile_definitions(
    traceloom_native_protected_sequence_fixture_reader_tests PRIVATE
    TRACELOOM_WORKSPACE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../.."
  )
  target_link_libraries(
    traceloom_native_protected_sequence_fixture_reader_tests PRIVATE
    traceloom_native_adapters
  )
  add_test(
    NAME traceloom_native_protected_sequence_fixture_reader_tests
    COMMAND traceloom_native_protected_sequence_fixture_reader_tests
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
