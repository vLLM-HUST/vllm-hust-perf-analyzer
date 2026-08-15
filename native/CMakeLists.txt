cmake_minimum_required(VERSION 3.16)

project(traceloom_native VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

option(TRACELOOM_NATIVE_BUILD_TESTS "Build TraceLoom native tests" ON)
option(
  TRACELOOM_NATIVE_ENABLE_SQLITE_ADAPTERS
  "Build optional SQLite profile adapters"
  ON
)
set(TRACELOOM_NATIVE_SQLITE_ADAPTERS_AVAILABLE OFF)

add_library(traceloom_native_core
  src/analysis/event_cost_attribution.cpp
  src/analysis/event_reconciliation.cpp
  src/analysis/event_reconciliation_task_communication.cpp
  src/analysis/exact_periodic_suffix.cpp
  src/analysis/flat_anchor_builder.cpp
  src/analysis/graph_body_cost_summary.cpp
  src/analysis/signal_classification_rules.cpp
  src/analysis/signal_classification_rules_io.cpp
  src/analysis/structural_occurrence_builder.cpp
  src/analysis/structural_occurrence_graph.cpp
  src/analysis/structural_occurrence_validation.cpp
  src/analysis/structural_symbol_normalization.cpp
  src/analysis/native_pipeline.cpp
  src/analysis/replay_body_pattern.cpp
  src/analysis/replay_internal_cost_map.cpp
  src/core/diagnostics.cpp
  src/core/sha256.cpp
  src/core/string_table.cpp
  src/cost/cost_summary_lite.cpp
  src/ir/anchor_table.cpp
  src/ir/capture_slot_table.cpp
  src/ir/captured_graph_instance_table.cpp
  src/ir/communication_op_table.cpp
  src/ir/graph_launch_occurrence_table.cpp
  src/ir/graph_launch_activity_table.cpp
  src/ir/graph_launch_body_table.cpp
  src/ir/graph_slot_template_table.cpp
  src/ir/graph_template_table.cpp
  src/ir/protected_interval_table.cpp
  src/ir/replay_composition_candidate_table.cpp
  src/ir/replay_unit_table.cpp
  src/ir/runtime_call_table.cpp
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
  src/analysis/anchor_graph_child_cost.cpp
  src/analysis/anchor_internal_cost_breakdown.cpp
  src/analysis/structural_cost_tree.cpp
  src/analysis/structural_cost_handoff.cpp
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

target_compile_definitions(
  traceloom_native_core
  PRIVATE
    TRACELOOM_SOURCE_DEFAULT_RULESET_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/default_signal_classification_rules.tsv"
    TRACELOOM_INSTALL_DEFAULT_RULESET_PATH="${CMAKE_INSTALL_FULL_DATADIR}/traceloom/default_signal_classification_rules.tsv"
    TRACELOOM_SOURCE_DEFAULT_SYMBOL_RULESET_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/default_structural_symbol_rules.tsv"
    TRACELOOM_INSTALL_DEFAULT_SYMBOL_RULESET_PATH="${CMAKE_INSTALL_FULL_DATADIR}/traceloom/default_structural_symbol_rules.tsv"
    TRACELOOM_SOURCE_DEFAULT_EVENT_RECONCILIATION_RULESET_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/default_event_reconciliation_rules.tsv"
    TRACELOOM_INSTALL_DEFAULT_EVENT_RECONCILIATION_RULESET_PATH="${CMAKE_INSTALL_FULL_DATADIR}/traceloom/default_event_reconciliation_rules.tsv"
)

install(
  FILES data/default_signal_classification_rules.tsv
  DESTINATION ${CMAKE_INSTALL_DATADIR}/traceloom
)
install(
  FILES data/default_structural_symbol_rules.tsv
  DESTINATION ${CMAKE_INSTALL_DATADIR}/traceloom
)
install(
  FILES data/default_event_reconciliation_rules.tsv
  DESTINATION ${CMAKE_INSTALL_DATADIR}/traceloom
)

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
  src/compat/anchor_sequence_rows.cpp
  src/compat/anchor_cost_breakdown_rows.cpp
  src/compat/augmented_catalog_materializer.cpp
  src/compat/augmented_projection_catalog.cpp
  src/compat/augmented_replay_projection_catalog.cpp
  src/compat/augmented_replay_body_projection_catalog.cpp
  src/compat/aux_attribution_rows.cpp
  src/compat/collective_tag_rows.cpp
  src/compat/exact_graph_sql_rows.cpp
  src/compat/native_graph_replay_rows.cpp
  src/compat/native_sidecar_materializer.cpp
  src/compat/native_sidecar_packaging.cpp
  src/compat/structural_projection_rows.cpp
  src/compat/structural_projection_tokens.cpp
  src/compat/replay_body_pattern_rows.cpp
  src/compat/replay_cost_sql_rows.cpp
  src/compat/replay_position_views.cpp
  src/compat/evidence_role_decision_rows.cpp
  src/compat/evidence_role_sql_rows.cpp
  src/compat/evidence_role_sql_writer.cpp
  src/compat/event_reconciliation_rows.cpp
  src/compat/runtime_device_rows.cpp
  src/compat/source_locator_json.cpp
  src/compat/schema.cpp
  src/compat/schema_structural_tables.cpp
  src/compat/schema_replay_tables.cpp
  src/compat/schema_replay_body_pattern_tables.cpp
  src/compat/sidecar_sqlite_utils.cpp
  src/compat/sidecar_collective_bindings.cpp
  src/compat/sidecar_collective_writer.cpp
  src/compat/sidecar_core_bindings.cpp
  src/compat/sidecar_core_writer.cpp
  src/compat/sidecar_graph_bindings.cpp
  src/compat/sidecar_graph_views.cpp
  src/compat/sidecar_graph_writer.cpp
  src/compat/sidecar_indexes.cpp
  src/compat/sidecar_runtime_views.cpp
  src/compat/sidecar_structure_bubble_views.cpp
  src/compat/sidecar_semantic_bindings.cpp
  src/compat/sidecar_semantic_views.cpp
  src/compat/sidecar_semantic_writer.cpp
  src/compat/sidecar_structure_bindings.cpp
  src/compat/sidecar_structure_writer.cpp
  src/compat/sidecar_symbol_normalization_views.cpp
  src/compat/sidecar_symbol_normalization_writer.cpp
  src/compat/sidecar_tree_views.cpp
  src/compat/sidecar_views.cpp
  src/compat/sidecar_writer.cpp
  src/compat/timeline_rows.cpp
  src/compat/symbol_normalization_rows.cpp
  src/materialize/grammar_debug_json.cpp
  src/materialize/loop_tree_markdown.cpp
)

target_link_libraries(traceloom_native_materialize PUBLIC traceloom_native_core)

if(MSVC)
  target_compile_options(traceloom_native_materialize PRIVATE /W4)
else()
  target_compile_options(
    traceloom_native_materialize PRIVATE -Wall -Wextra -Wpedantic
  )
endif()

if(TRACELOOM_NATIVE_ENABLE_SQLITE_ADAPTERS)
  find_package(SQLite3 QUIET)
  if(SQLite3_FOUND)
    target_sources(traceloom_native_adapters
      PRIVATE
        src/adapters/ascend_sqlite_adapter.cpp
        src/adapters/ascend_sqlite_evidence.cpp
        src/adapters/ascend_sqlite_foundation.cpp
        src/adapters/ascend_sqlite_graph.cpp
        src/adapters/ascend_sqlite_replay.cpp
        src/adapters/ascend_sqlite_split.cpp
        src/adapters/ascend_sqlite_tasks.cpp
        src/adapters/cuda_nsys_sqlite_adapter.cpp
        src/adapters/cuda_nsys_sqlite_foundation.cpp
        src/adapters/cuda_nsys_sqlite_graph.cpp
        src/adapters/hygon_sqlite_adapter.cpp
        src/adapters/sqlite_profile_reader.cpp
    )
    if(TARGET SQLite::SQLite3)
      target_link_libraries(traceloom_native_adapters PRIVATE SQLite::SQLite3)
      target_link_libraries(traceloom_native_materialize PRIVATE SQLite::SQLite3)
    else()
      target_include_directories(
        traceloom_native_adapters PRIVATE ${SQLite3_INCLUDE_DIRS}
      )
      target_include_directories(
        traceloom_native_materialize PRIVATE ${SQLite3_INCLUDE_DIRS}
      )
      target_link_libraries(
        traceloom_native_adapters PRIVATE ${SQLite3_LIBRARIES}
      )
      target_link_libraries(
        traceloom_native_materialize PRIVATE ${SQLite3_LIBRARIES}
      )
    endif()
    target_compile_definitions(
      traceloom_native_adapters
      PUBLIC TRACELOOM_NATIVE_HAS_SQLITE_ADAPTERS=1
    )
    target_compile_definitions(
      traceloom_native_materialize
      PRIVATE TRACELOOM_NATIVE_HAS_SQLITE_COMPAT=1
    )
    set(TRACELOOM_NATIVE_SQLITE_ADAPTERS_AVAILABLE ON)
  else()
    message(STATUS "SQLite3 not found; SQLite source adapters are disabled")
  endif()
endif()

if(MSVC)
  target_compile_options(traceloom_native_adapters PRIVATE /W4)
else()
  target_compile_options(traceloom_native_adapters PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(TRACELOOM_NATIVE_SQLITE_ADAPTERS_AVAILABLE)
  add_executable(traceloom-native-ascend-sqlite-inventory
    src/tools/ascend_sqlite_inventory_main.cpp
  )
  target_link_libraries(
    traceloom-native-ascend-sqlite-inventory PRIVATE traceloom_native_adapters
  )

  add_executable(traceloom
    src/tools/analyze_db_main.cpp
  )
  target_link_libraries(
    traceloom PRIVATE
    traceloom_native_adapters
    traceloom_native_materialize
  )

  target_compile_definitions(
    traceloom PRIVATE TRACELOOM_NATIVE_VERSION="${PROJECT_VERSION}"
  )

  install(
    TARGETS traceloom
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )

  set(CPACK_GENERATOR "DEB")
  set(CPACK_PACKAGE_NAME "traceloom-native")
  set(CPACK_PACKAGE_VENDOR "vLLM-HUST")
  set(CPACK_PACKAGE_CONTACT "vLLM-HUST")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
      "Native TraceLoom analyzer for profiler SQLite databases")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "vLLM-HUST")
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6, libsqlite3-0")
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
  include(CPack)
endif()

add_executable(traceloom-native-analyze-fixture
  src/tools/analyze_fixture_main.cpp
)
target_link_libraries(
  traceloom-native-analyze-fixture PRIVATE
  traceloom_native_adapters
  traceloom_native_materialize
)

if(TRACELOOM_NATIVE_BUILD_TESTS)
  enable_testing()

  set(
    TRACELOOM_NATIVE_FIXTURE_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
  )

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

  add_executable(traceloom_native_event_reconciliation_tests
    tests/analysis/event_reconciliation_tests.cpp
  )
  target_link_libraries(
    traceloom_native_event_reconciliation_tests PRIVATE traceloom_native_core
  )
  add_test(
    NAME traceloom_native_event_reconciliation_tests
    COMMAND traceloom_native_event_reconciliation_tests
  )

  add_executable(traceloom_native_structural_symbol_normalization_tests
    tests/analysis/structural_symbol_normalization_tests.cpp
  )
  target_link_libraries(
    traceloom_native_structural_symbol_normalization_tests PRIVATE
    traceloom_native_core
  )
  add_test(
    NAME traceloom_native_structural_symbol_normalization_tests
    COMMAND traceloom_native_structural_symbol_normalization_tests
  )

  add_executable(traceloom_native_signal_classification_rules_tests
    tests/analysis/signal_classification_rules_tests.cpp
  )
  target_link_libraries(
    traceloom_native_signal_classification_rules_tests PRIVATE traceloom_native_core
  )
  add_test(
    NAME traceloom_native_signal_classification_rules_tests
    COMMAND traceloom_native_signal_classification_rules_tests
  )

  add_executable(traceloom_native_graph_body_cost_summary_tests
    tests/analysis/graph_body_cost_summary_tests.cpp
  )
  target_link_libraries(
    traceloom_native_graph_body_cost_summary_tests PRIVATE
    traceloom_native_core
  )
  add_test(
    NAME traceloom_native_graph_body_cost_summary_tests
    COMMAND traceloom_native_graph_body_cost_summary_tests
  )

  add_executable(traceloom_native_replay_internal_cost_map_tests
    tests/analysis/replay_internal_cost_map_contract_tests.cpp
    tests/analysis/replay_internal_cost_map_happy_path_tests.cpp
    tests/analysis/replay_internal_cost_map_tests.cpp
  )
  target_link_libraries(
    traceloom_native_replay_internal_cost_map_tests PRIVATE
    traceloom_native_core
  )
  add_test(
    NAME traceloom_native_replay_internal_cost_map_tests
    COMMAND traceloom_native_replay_internal_cost_map_tests
  )

  add_executable(traceloom_native_replay_body_pattern_tests
    tests/analysis/replay_body_pattern_tests.cpp
  )
  target_link_libraries(
    traceloom_native_replay_body_pattern_tests PRIVATE traceloom_native_core
  )
  add_test(
    NAME traceloom_native_replay_body_pattern_tests
    COMMAND traceloom_native_replay_body_pattern_tests
  )

  add_executable(traceloom_native_sha256_tests
    tests/core/sha256_tests.cpp
  )
  target_link_libraries(
    traceloom_native_sha256_tests PRIVATE traceloom_native_core
  )
  add_test(
    NAME traceloom_native_sha256_tests
    COMMAND traceloom_native_sha256_tests
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

  add_executable(traceloom_native_structural_occurrence_builder_tests
    tests/analysis/structural_occurrence_builder_tests.cpp
  )
  target_link_libraries(
    traceloom_native_structural_occurrence_builder_tests PRIVATE traceloom_native_core
  )
  add_test(
    NAME traceloom_native_structural_occurrence_builder_tests
    COMMAND traceloom_native_structural_occurrence_builder_tests
  )

  add_executable(traceloom_native_structural_occurrence_visibility_tests
    tests/analysis/structural_occurrence_visibility_tests.cpp
  )
  target_link_libraries(
    traceloom_native_structural_occurrence_visibility_tests PRIVATE traceloom_native_core
  )
  add_test(
    NAME traceloom_native_structural_occurrence_visibility_tests
    COMMAND traceloom_native_structural_occurrence_visibility_tests
  )

  add_executable(traceloom_native_structural_occurrence_graph_boundary_tests
    tests/analysis/structural_occurrence_graph_boundary_tests.cpp
  )
  target_link_libraries(
    traceloom_native_structural_occurrence_graph_boundary_tests PRIVATE
    traceloom_native_core
  )
  add_test(
    NAME traceloom_native_structural_occurrence_graph_boundary_tests
    COMMAND traceloom_native_structural_occurrence_graph_boundary_tests
  )

  add_executable(traceloom_native_structural_cost_handoff_tests
    tests/analysis/structural_cost_handoff_tests.cpp
  )
  target_link_libraries(
    traceloom_native_structural_cost_handoff_tests PRIVATE
    traceloom_native_core
  )
  add_test(
    NAME traceloom_native_structural_cost_handoff_tests
    COMMAND traceloom_native_structural_cost_handoff_tests
  )

  add_executable(traceloom_native_structural_cost_tree_tests
    tests/analysis/structural_cost_tree_tests.cpp
  )
  target_link_libraries(
    traceloom_native_structural_cost_tree_tests PRIVATE traceloom_native_core
  )
  add_test(
    NAME traceloom_native_structural_cost_tree_tests
    COMMAND traceloom_native_structural_cost_tree_tests
  )

  add_executable(traceloom_native_anchor_internal_cost_breakdown_tests
    tests/analysis/anchor_internal_cost_breakdown_tests.cpp
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
    tests/analysis/anchor_graph_child_cost_tests.cpp
  )
  target_link_libraries(
    traceloom_native_anchor_graph_child_cost_tests PRIVATE traceloom_native_core
  )
  add_test(
    NAME traceloom_native_anchor_graph_child_cost_tests
    COMMAND traceloom_native_anchor_graph_child_cost_tests
  )

  add_executable(traceloom_native_loop_tree_markdown_tests
    tests/materialize/loop_tree_markdown_tests.cpp
  )
  target_link_libraries(
    traceloom_native_loop_tree_markdown_tests PRIVATE
    traceloom_native_materialize
  )
  add_test(
    NAME traceloom_native_loop_tree_markdown_tests
    COMMAND traceloom_native_loop_tree_markdown_tests
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

  add_executable(traceloom_native_anchor_sequence_rows_tests
    tests/compat/anchor_sequence_rows_tests.cpp
  )
  target_link_libraries(
    traceloom_native_anchor_sequence_rows_tests PRIVATE
    traceloom_native_materialize
  )
  add_test(
    NAME traceloom_native_anchor_sequence_rows_tests
    COMMAND traceloom_native_anchor_sequence_rows_tests
  )

  add_executable(traceloom_native_symbol_normalization_rows_tests
    tests/compat/symbol_normalization_rows_tests.cpp
  )
  target_link_libraries(
    traceloom_native_symbol_normalization_rows_tests PRIVATE
    traceloom_native_materialize
  )
  add_test(
    NAME traceloom_native_symbol_normalization_rows_tests
    COMMAND traceloom_native_symbol_normalization_rows_tests
  )

  add_executable(traceloom_native_aux_attribution_rows_tests
    tests/compat/aux_attribution_rows_tests.cpp
  )
  target_link_libraries(
    traceloom_native_aux_attribution_rows_tests PRIVATE
    traceloom_native_materialize
  )
  add_test(
    NAME traceloom_native_aux_attribution_rows_tests
    COMMAND traceloom_native_aux_attribution_rows_tests
  )

  if(TRACELOOM_NATIVE_SQLITE_ADAPTERS_AVAILABLE)
    add_executable(traceloom_native_runtime_device_rows_tests
      tests/compat/runtime_device_rows_tests.cpp
    )
    target_link_libraries(
      traceloom_native_runtime_device_rows_tests PRIVATE
      traceloom_native_materialize
    )
    if(TARGET SQLite::SQLite3)
      target_link_libraries(
        traceloom_native_runtime_device_rows_tests PRIVATE SQLite::SQLite3
      )
    else()
      target_include_directories(
        traceloom_native_runtime_device_rows_tests
        PRIVATE ${SQLite3_INCLUDE_DIRS}
      )
      target_link_libraries(
        traceloom_native_runtime_device_rows_tests PRIVATE ${SQLite3_LIBRARIES}
      )
    endif()
    add_test(
      NAME traceloom_native_runtime_device_rows_tests
      COMMAND traceloom_native_runtime_device_rows_tests
    )
  endif()

  add_executable(traceloom_native_collective_tag_rows_tests
    tests/compat/collective_tag_rows_tests.cpp
  )
  target_link_libraries(
    traceloom_native_collective_tag_rows_tests PRIVATE
    traceloom_native_materialize
  )
  add_test(
    NAME traceloom_native_collective_tag_rows_tests
    COMMAND traceloom_native_collective_tag_rows_tests
  )

  add_executable(traceloom_native_structural_projection_rows_tests
    tests/compat/structural_projection_rows_tests.cpp
  )
  target_link_libraries(
    traceloom_native_structural_projection_rows_tests PRIVATE
    traceloom_native_materialize
  )
  add_test(
    NAME traceloom_native_structural_projection_rows_tests
    COMMAND traceloom_native_structural_projection_rows_tests
  )

  add_executable(traceloom_native_timeline_rows_tests
    tests/compat/timeline_rows_tests.cpp
  )
  target_link_libraries(
    traceloom_native_timeline_rows_tests PRIVATE
    traceloom_native_materialize
  )
  add_test(
    NAME traceloom_native_timeline_rows_tests
    COMMAND traceloom_native_timeline_rows_tests
  )

  add_executable(traceloom_native_exact_graph_sql_rows_tests
    tests/compat/exact_graph_sql_rows_tests.cpp
  )
  target_link_libraries(
    traceloom_native_exact_graph_sql_rows_tests PRIVATE
    traceloom_native_materialize
  )
  add_test(
    NAME traceloom_native_exact_graph_sql_rows_tests
    COMMAND traceloom_native_exact_graph_sql_rows_tests
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
      ${TRACELOOM_NATIVE_FIXTURE_ROOT}/protected_sequence/f1_tiny_repeated_pair.json
      --threads 2
      --grammar-debug-out -
  )
  set_tests_properties(
    traceloom_native_analyze_fixture_grammar_debug_cli
    PROPERTIES PASS_REGULAR_EXPRESSION "native_grammar_debug_v1"
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
    TRACELOOM_NATIVE_FIXTURE_ROOT="${TRACELOOM_NATIVE_FIXTURE_ROOT}"
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
    TRACELOOM_NATIVE_FIXTURE_ROOT="${TRACELOOM_NATIVE_FIXTURE_ROOT}"
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
    TRACELOOM_NATIVE_FIXTURE_ROOT="${TRACELOOM_NATIVE_FIXTURE_ROOT}"
  )
  target_link_libraries(
    traceloom_native_protected_sequence_fixture_reader_tests PRIVATE
    traceloom_native_adapters
  )
  add_test(
    NAME traceloom_native_protected_sequence_fixture_reader_tests
    COMMAND traceloom_native_protected_sequence_fixture_reader_tests
  )

  if(TRACELOOM_NATIVE_SQLITE_ADAPTERS_AVAILABLE)
    add_test(
      NAME traceloom_native_rejects_removed_json_option
      COMMAND ${CMAKE_COMMAND}
        -DTRACELOOM_ANALYZER=$<TARGET_FILE:traceloom>
        -DTRACELOOM_REMOVED_JSON_PATH=${CMAKE_CURRENT_BINARY_DIR}/removed_native_result.json
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/boundary/reject_removed_native_json_option.cmake
    )

    function(traceloom_add_ascend_sqlite_test target source)
      add_executable(${target}
        ${source}
        tests/support/ascend_sqlite_fixture.cpp
        tests/support/sqlite_fixture.cpp
      )
      target_compile_definitions(
        ${target} PRIVATE
        TRACELOOM_NATIVE_FIXTURE_ROOT="${TRACELOOM_NATIVE_FIXTURE_ROOT}"
      )
      target_include_directories(
        ${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests
      )
      target_link_libraries(
        ${target} PRIVATE traceloom_native_adapters
      )
      if(TARGET SQLite::SQLite3)
        target_link_libraries(${target} PRIVATE SQLite::SQLite3)
      else()
        target_include_directories(${target} PRIVATE ${SQLite3_INCLUDE_DIRS})
        target_link_libraries(${target} PRIVATE ${SQLite3_LIBRARIES})
      endif()
      add_test(NAME ${target} COMMAND ${target})
    endfunction()

    traceloom_add_ascend_sqlite_test(
      traceloom_native_ascend_sqlite_adapter_tests
      tests/adapters/ascend_sqlite_adapter_tests.cpp
    )
    traceloom_add_ascend_sqlite_test(
      traceloom_native_ascend_sqlite_graph_tests
      tests/adapters/ascend_sqlite_graph_tests.cpp
    )
    traceloom_add_ascend_sqlite_test(
      traceloom_native_ascend_sqlite_replay_composition_tests
      tests/adapters/ascend_sqlite_replay_composition_tests.cpp
    )

    add_executable(traceloom_native_ascend_sqlite_determinism_tests
      tests/adapters/ascend_sqlite_determinism_tests.cpp
    )
    target_link_libraries(
      traceloom_native_ascend_sqlite_determinism_tests PRIVATE
      traceloom_native_adapters
    )
    if(TARGET SQLite::SQLite3)
      target_link_libraries(
        traceloom_native_ascend_sqlite_determinism_tests PRIVATE
        SQLite::SQLite3
      )
    else()
      target_include_directories(
        traceloom_native_ascend_sqlite_determinism_tests
        PRIVATE ${SQLite3_INCLUDE_DIRS}
      )
      target_link_libraries(
        traceloom_native_ascend_sqlite_determinism_tests PRIVATE
        ${SQLite3_LIBRARIES}
      )
    endif()
    add_test(
      NAME traceloom_native_ascend_sqlite_determinism_tests
      COMMAND traceloom_native_ascend_sqlite_determinism_tests
    )

    add_executable(traceloom_native_hygon_sqlite_adapter_tests
      tests/adapters/hygon_sqlite_adapter_tests.cpp
    )
    target_link_libraries(
      traceloom_native_hygon_sqlite_adapter_tests PRIVATE
      traceloom_native_adapters
    )
    if(TARGET SQLite::SQLite3)
      target_link_libraries(
        traceloom_native_hygon_sqlite_adapter_tests PRIVATE SQLite::SQLite3
      )
    else()
      target_include_directories(
        traceloom_native_hygon_sqlite_adapter_tests
        PRIVATE ${SQLite3_INCLUDE_DIRS}
      )
      target_link_libraries(
        traceloom_native_hygon_sqlite_adapter_tests
        PRIVATE ${SQLite3_LIBRARIES}
      )
    endif()
    add_test(
      NAME traceloom_native_hygon_sqlite_adapter_tests
      COMMAND traceloom_native_hygon_sqlite_adapter_tests
    )

    add_executable(traceloom_native_cuda_nsys_sqlite_adapter_tests
      tests/adapters/cuda_nsys_sqlite_adapter_tests.cpp
    )
    target_link_libraries(
      traceloom_native_cuda_nsys_sqlite_adapter_tests PRIVATE
      traceloom_native_adapters
    )
    if(TARGET SQLite::SQLite3)
      target_link_libraries(
        traceloom_native_cuda_nsys_sqlite_adapter_tests PRIVATE SQLite::SQLite3
      )
    else()
      target_include_directories(
        traceloom_native_cuda_nsys_sqlite_adapter_tests
        PRIVATE ${SQLite3_INCLUDE_DIRS}
      )
      target_link_libraries(
        traceloom_native_cuda_nsys_sqlite_adapter_tests
        PRIVATE ${SQLite3_LIBRARIES}
      )
    endif()
    add_test(
      NAME traceloom_native_cuda_nsys_sqlite_adapter_tests
      COMMAND traceloom_native_cuda_nsys_sqlite_adapter_tests
        ${CMAKE_CURRENT_BINARY_DIR}/cuda_nsys_minimal_fixture.sqlite
    )

    add_executable(traceloom_native_sqlite_profile_reader_tests
      tests/adapters/sqlite_profile_reader_tests.cpp
    )
    target_include_directories(
      traceloom_native_sqlite_profile_reader_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src/adapters
    )
    target_link_libraries(
      traceloom_native_sqlite_profile_reader_tests PRIVATE
      traceloom_native_adapters
    )
    if(TARGET SQLite::SQLite3)
      target_link_libraries(
        traceloom_native_sqlite_profile_reader_tests PRIVATE SQLite::SQLite3
      )
    else()
      target_include_directories(
        traceloom_native_sqlite_profile_reader_tests
        PRIVATE ${SQLite3_INCLUDE_DIRS}
      )
      target_link_libraries(
        traceloom_native_sqlite_profile_reader_tests
        PRIVATE ${SQLite3_LIBRARIES}
      )
    endif()
    add_test(
      NAME traceloom_native_sqlite_profile_reader_tests
      COMMAND traceloom_native_sqlite_profile_reader_tests
    )

    add_executable(traceloom_native_sidecar_writer_tests
      tests/compat/sidecar_writer_tests.cpp
      tests/compat/sidecar_writer_graph_projection_tests.cpp
      tests/compat/sidecar_writer_test_support.cpp
    )
    target_compile_definitions(
      traceloom_native_sidecar_writer_tests PRIVATE
      TRACELOOM_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
    )
    target_link_libraries(
      traceloom_native_sidecar_writer_tests PRIVATE
      traceloom_native_materialize
    )
    if(TARGET SQLite::SQLite3)
      target_link_libraries(
        traceloom_native_sidecar_writer_tests PRIVATE SQLite::SQLite3
      )
    else()
      target_include_directories(
        traceloom_native_sidecar_writer_tests
        PRIVATE ${SQLite3_INCLUDE_DIRS}
      )
      target_link_libraries(
        traceloom_native_sidecar_writer_tests
        PRIVATE ${SQLite3_LIBRARIES}
      )
    endif()
    add_test(
      NAME traceloom_native_sidecar_writer_tests
      COMMAND traceloom_native_sidecar_writer_tests
    )

    add_executable(traceloom_native_sidecar_materializer_tests
      tests/compat/native_sidecar_materializer_tests.cpp
      tests/compat/native_sidecar_materializer_graph_tests.cpp
      tests/compat/native_sidecar_materializer_host_window_tests.cpp
      tests/compat/native_sidecar_materializer_packaging_tests.cpp
      tests/compat/native_sidecar_materializer_test_support.cpp
    )
    target_link_libraries(
      traceloom_native_sidecar_materializer_tests PRIVATE
      traceloom_native_materialize
    )
    if(TARGET SQLite::SQLite3)
      target_link_libraries(
        traceloom_native_sidecar_materializer_tests PRIVATE SQLite::SQLite3
      )
    else()
      target_include_directories(
        traceloom_native_sidecar_materializer_tests
        PRIVATE ${SQLite3_INCLUDE_DIRS}
      )
      target_link_libraries(
        traceloom_native_sidecar_materializer_tests
        PRIVATE ${SQLite3_LIBRARIES}
      )
    endif()
    add_test(
      NAME traceloom_native_sidecar_materializer_tests
      COMMAND traceloom_native_sidecar_materializer_tests
    )

    add_executable(traceloom_native_event_reconciliation_rows_tests
      tests/compat/event_reconciliation_rows_tests.cpp
    )
    target_link_libraries(
      traceloom_native_event_reconciliation_rows_tests PRIVATE
      traceloom_native_materialize
    )
    if(TARGET SQLite::SQLite3)
      target_link_libraries(
        traceloom_native_event_reconciliation_rows_tests PRIVATE
        SQLite::SQLite3
      )
    else()
      target_include_directories(
        traceloom_native_event_reconciliation_rows_tests PRIVATE
        ${SQLite3_INCLUDE_DIRS}
      )
      target_link_libraries(
        traceloom_native_event_reconciliation_rows_tests PRIVATE
        ${SQLite3_LIBRARIES}
      )
    endif()
    add_test(
      NAME traceloom_native_event_reconciliation_rows_tests
      COMMAND traceloom_native_event_reconciliation_rows_tests
    )

    add_executable(traceloom_native_report_sql_compat_tests
      tests/compat/report_sql_compat_tests.cpp
    )
    target_compile_definitions(
      traceloom_native_report_sql_compat_tests PRIVATE
      TRACELOOM_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
    )
    if(TARGET SQLite::SQLite3)
      target_link_libraries(
        traceloom_native_report_sql_compat_tests PRIVATE SQLite::SQLite3
      )
    else()
      target_include_directories(
        traceloom_native_report_sql_compat_tests
        PRIVATE ${SQLite3_INCLUDE_DIRS}
      )
      target_link_libraries(
        traceloom_native_report_sql_compat_tests
        PRIVATE ${SQLite3_LIBRARIES}
      )
    endif()
    add_test(
      NAME traceloom_native_report_sql_compat_tests
      COMMAND traceloom_native_report_sql_compat_tests
    )

    add_executable(traceloom_native_evidence_role_materialization_tests
      tests/compat/evidence_role_materialization_tests.cpp
    )
    target_link_libraries(
      traceloom_native_evidence_role_materialization_tests PRIVATE
      traceloom_native_materialize
    )
    if(TARGET SQLite::SQLite3)
      target_link_libraries(
        traceloom_native_evidence_role_materialization_tests
        PRIVATE SQLite::SQLite3
      )
    else()
      target_include_directories(
        traceloom_native_evidence_role_materialization_tests
        PRIVATE ${SQLite3_INCLUDE_DIRS}
      )
      target_link_libraries(
        traceloom_native_evidence_role_materialization_tests
        PRIVATE ${SQLite3_LIBRARIES}
      )
    endif()
    add_test(
      NAME traceloom_native_evidence_role_materialization_tests
      COMMAND traceloom_native_evidence_role_materialization_tests
    )

    add_executable(traceloom_native_generated_sidecar_projection_tests
      tests/compat/generated_sidecar_projection_tests.cpp
    )
    target_compile_definitions(
      traceloom_native_generated_sidecar_projection_tests PRIVATE
      TRACELOOM_NATIVE_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(
      traceloom_native_generated_sidecar_projection_tests PRIVATE
      traceloom_native_adapters
      traceloom_native_materialize
    )
    if(TARGET SQLite::SQLite3)
      target_link_libraries(
        traceloom_native_generated_sidecar_projection_tests PRIVATE SQLite::SQLite3
      )
    else()
      target_include_directories(
        traceloom_native_generated_sidecar_projection_tests
        PRIVATE ${SQLite3_INCLUDE_DIRS}
      )
      target_link_libraries(
        traceloom_native_generated_sidecar_projection_tests
        PRIVATE ${SQLite3_LIBRARIES}
      )
    endif()
    add_test(
      NAME traceloom_native_generated_sidecar_projection_tests
      COMMAND traceloom_native_generated_sidecar_projection_tests
    )

    add_executable(traceloom_native_cli_sidecar_smoke_tests
      tests/compat/cli_sidecar_smoke_tests.cpp
    )
    target_link_libraries(
      traceloom_native_cli_sidecar_smoke_tests PRIVATE traceloom_native_core
    )
    if(TARGET SQLite::SQLite3)
      target_link_libraries(
        traceloom_native_cli_sidecar_smoke_tests PRIVATE SQLite::SQLite3
      )
    else()
      target_include_directories(
        traceloom_native_cli_sidecar_smoke_tests
        PRIVATE ${SQLite3_INCLUDE_DIRS}
      )
      target_link_libraries(
        traceloom_native_cli_sidecar_smoke_tests PRIVATE ${SQLite3_LIBRARIES}
      )
    endif()

    set(
      TRACELOOM_OFFICIAL_ASCEND_CLI_INPUT
      ${CMAKE_CURRENT_BINARY_DIR}/official_ascend_cli_input/ascend_pytorch_profiler_0.db
    )
    add_test(
      NAME traceloom_native_prepare_official_ascend_cli_input
      COMMAND traceloom_native_ascend_sqlite_adapter_tests
        --materialize ${TRACELOOM_OFFICIAL_ASCEND_CLI_INPUT}
    )
    add_test(
      NAME traceloom_native_analyze_official_ascend_cli_input
      COMMAND traceloom
        ${TRACELOOM_OFFICIAL_ASCEND_CLI_INPUT}
        --threads 2
        --timings
        --sidecar-only
        --no-aug-db
        --compat-sidecar-out
        ${CMAKE_CURRENT_BINARY_DIR}/official_ascend_cli_compat.db
    )
    set_tests_properties(
      traceloom_native_analyze_official_ascend_cli_input
      PROPERTIES
        DEPENDS traceloom_native_prepare_official_ascend_cli_input
        PASS_REGULAR_EXPRESSION "timing source_kind=ascend_sqlite_hot_path"
    )
    add_test(
      NAME traceloom_native_analyze_official_ascend_cli_sidecar
      COMMAND traceloom_native_cli_sidecar_smoke_tests
        ${CMAKE_CURRENT_BINARY_DIR}/official_ascend_cli_compat.db fixture
    )
    set_tests_properties(
      traceloom_native_analyze_official_ascend_cli_sidecar
      PROPERTIES DEPENDS traceloom_native_analyze_official_ascend_cli_input
    )

    add_test(
      NAME traceloom_native_analyze_cuda_nsys_compat_sidecar_cli
      COMMAND traceloom
        --source-kind cuda_nsys_sqlite
        --source-db ${CMAKE_CURRENT_BINARY_DIR}/cuda_nsys_minimal_fixture.sqlite
        --threads 2
        --timings
        --sidecar-only
        --no-aug-db
        --compat-sidecar-out ${CMAKE_CURRENT_BINARY_DIR}/cuda_nsys_cli_compat.db
        --loop-tree-out ${CMAKE_CURRENT_BINARY_DIR}/cuda_nsys_loop_tree_v2.md
        --loop-tree-device-id 0
    )
    set_tests_properties(
      traceloom_native_analyze_cuda_nsys_compat_sidecar_cli
      PROPERTIES
        DEPENDS traceloom_native_cuda_nsys_sqlite_adapter_tests
        PASS_REGULAR_EXPRESSION "timing source_kind=cuda_nsys_sqlite"
    )
    add_test(
      NAME traceloom_native_analyze_cuda_nsys_compat_sidecar_db
      COMMAND traceloom_native_cli_sidecar_smoke_tests
        ${CMAKE_CURRENT_BINARY_DIR}/cuda_nsys_cli_compat.db cuda
        ${CMAKE_CURRENT_BINARY_DIR}/cuda_nsys_loop_tree_v2.md
    )
    set_tests_properties(
      traceloom_native_analyze_cuda_nsys_compat_sidecar_db
      PROPERTIES
        DEPENDS traceloom_native_analyze_cuda_nsys_compat_sidecar_cli
    )

    add_test(
      NAME traceloom_native_prepare_cuda_nsys_default_input_dir
      COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_CURRENT_BINARY_DIR}/default_cli_input
    )
    add_test(
      NAME traceloom_native_prepare_cuda_nsys_default_input
      COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_BINARY_DIR}/cuda_nsys_minimal_fixture.sqlite
        ${CMAKE_CURRENT_BINARY_DIR}/default_cli_input/profile.sqlite
    )
    set_tests_properties(
      traceloom_native_prepare_cuda_nsys_default_input
      PROPERTIES
        DEPENDS
          "traceloom_native_cuda_nsys_sqlite_adapter_tests;traceloom_native_prepare_cuda_nsys_default_input_dir"
    )
    add_test(
      NAME traceloom_native_analyze_cuda_nsys_default_augmented_db_cli
      COMMAND traceloom
        ${CMAKE_CURRENT_BINARY_DIR}/default_cli_input/profile.sqlite
        --threads 2
        --classification-rule-override
        cuda.anchor.task.type.cuda.kernel.51cc7472.priority=96
    )
    set_tests_properties(
      traceloom_native_analyze_cuda_nsys_default_augmented_db_cli
      PROPERTIES
        DEPENDS traceloom_native_prepare_cuda_nsys_default_input
        PASS_REGULAR_EXPRESSION
          "wrote queryable database timeline: .*default_cli_input/traceloom/analysis.db"
    )
    add_test(
      NAME traceloom_native_analyze_cuda_nsys_default_augmented_db
      COMMAND traceloom_native_cli_sidecar_smoke_tests
        ${CMAKE_CURRENT_BINARY_DIR}/default_cli_input/traceloom/analysis.db
        augmented
    )
    set_tests_properties(
      traceloom_native_analyze_cuda_nsys_default_augmented_db
      PROPERTIES
        DEPENDS traceloom_native_analyze_cuda_nsys_default_augmented_db_cli
    )

    add_test(
      NAME traceloom_native_analyze_fixture_compat_sidecar_cli
      COMMAND traceloom-native-analyze-fixture
        --fixture
        ${TRACELOOM_NATIVE_FIXTURE_ROOT}/protected_sequence/f1_tiny_repeated_pair.json
        --threads 2
        --compat-sidecar-out ${CMAKE_CURRENT_BINARY_DIR}/fixture_cli_compat.db
    )
    add_test(
      NAME traceloom_native_analyze_fixture_compat_sidecar_db
      COMMAND traceloom_native_cli_sidecar_smoke_tests
        ${CMAKE_CURRENT_BINARY_DIR}/fixture_cli_compat.db fixture
    )
    set_tests_properties(
      traceloom_native_analyze_fixture_compat_sidecar_db
      PROPERTIES DEPENDS traceloom_native_analyze_fixture_compat_sidecar_cli
    )

  endif()

  add_test(
    NAME traceloom_native_include_boundary_tests
    COMMAND ${CMAKE_COMMAND}
      -DTRACELOOM_NATIVE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/boundary/include_boundary_tests.cmake
  )
endif()
