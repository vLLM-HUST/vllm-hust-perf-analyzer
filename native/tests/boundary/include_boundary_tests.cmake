if(NOT DEFINED TRACELOOM_NATIVE_ROOT)
  message(FATAL_ERROR "TRACELOOM_NATIVE_ROOT is required")
endif()

set(_core_dirs
  analysis
  core
  ir
  runtime
  sequence
  pattern
  graph
  cost
)

set(_forbidden_patterns
  "sqlite3\\.h"
  "Python\\.h"
  "nlohmann/json\\.hpp"
  "CLI11/"
  "traceloom/adapters/"
  "SELECT[ \n\t]"
  "FROM[ \n\t]"
  "JOIN[ \n\t]"
  "sqlite_"
  "msprof"
  "COMPUTE_TASK_INFO"
)

foreach(_dir IN LISTS _core_dirs)
  foreach(_base
      "${TRACELOOM_NATIVE_ROOT}/include/traceloom/${_dir}"
      "${TRACELOOM_NATIVE_ROOT}/src/${_dir}")
    if(EXISTS "${_base}")
      file(GLOB_RECURSE _files
        "${_base}/*.h"
        "${_base}/*.hpp"
        "${_base}/*.cc"
        "${_base}/*.cpp"
      )

      foreach(_file IN LISTS _files)
        file(READ "${_file}" _content)
        foreach(_pattern IN LISTS _forbidden_patterns)
          if(_content MATCHES "${_pattern}")
            message(FATAL_ERROR
              "Forbidden dependency '${_pattern}' found in ${_file}")
          endif()
        endforeach()
      endforeach()
    endif()
  endforeach()
endforeach()

set(_removed_debt_paths
  "${TRACELOOM_NATIVE_ROOT}/include/traceloom/report"
  "${TRACELOOM_NATIVE_ROOT}/src/report"
  "${TRACELOOM_NATIVE_ROOT}/tests/report"
  "${TRACELOOM_NATIVE_ROOT}/include/traceloom/report/report_tree.h"
  "${TRACELOOM_NATIVE_ROOT}/include/traceloom/report/report_tree_builder.h"
  "${TRACELOOM_NATIVE_ROOT}/include/traceloom/compat/report_tree_rows.h"
  "${TRACELOOM_NATIVE_ROOT}/src/report/report_tree.cpp"
  "${TRACELOOM_NATIVE_ROOT}/src/report/report_tree_builder.cpp"
  "${TRACELOOM_NATIVE_ROOT}/src/compat/report_tree_rows.cpp"
  "${TRACELOOM_NATIVE_ROOT}/include/traceloom/compat/aclgraph_graph_replay_rows.h"
  "${TRACELOOM_NATIVE_ROOT}/src/compat/aclgraph_graph_replay_rows.cpp"
  "${TRACELOOM_NATIVE_ROOT}/tests/compat/aclgraph_graph_replay_rows_tests.cpp"
  "${TRACELOOM_NATIVE_ROOT}/include/traceloom/materialize/native_result_json.h"
  "${TRACELOOM_NATIVE_ROOT}/src/materialize/native_result_json.cpp"
  "${TRACELOOM_NATIVE_ROOT}/tests/materialize/native_result_json_tests.cpp"
  "${TRACELOOM_NATIVE_ROOT}/src/tools/analyze_aclgraph_fixture_main.cpp"
)

foreach(_path IN LISTS _removed_debt_paths)
  if(EXISTS "${_path}")
    message(FATAL_ERROR "Removed compatibility debt was reintroduced: ${_path}")
  endif()
endforeach()

file(GLOB_RECURSE _native_cpp_files
  "${TRACELOOM_NATIVE_ROOT}/include/*.h"
  "${TRACELOOM_NATIVE_ROOT}/include/*.hpp"
  "${TRACELOOM_NATIVE_ROOT}/src/*.cc"
  "${TRACELOOM_NATIVE_ROOT}/src/*.cpp"
  "${TRACELOOM_NATIVE_ROOT}/tests/*.cc"
  "${TRACELOOM_NATIVE_ROOT}/tests/*.cpp"
)

set(_removed_debt_patterns
  "traceloom/report/"
  "Report(Tree|Token|Node|Anchor|Cost)"
  "build_native_device_report_trees"
  "build_report_tree_"
  "materialize_report_compatibility_"
  "drop_report_compatibility_views"
  "build_aclgraph_fixture_graph_replay_sql_rows"
  "write_aclgraph_fixture_compatibility_sidecar"
  "native_result_json"
  "NativeResultJson"
  "native_in_memory_result_v1"
  "write_native_result_json"
)

foreach(_file IN LISTS _native_cpp_files)
  file(READ "${_file}" _content)
  foreach(_pattern IN LISTS _removed_debt_patterns)
    if(_content MATCHES "${_pattern}")
      message(FATAL_ERROR
        "Removed compatibility debt '${_pattern}' found in ${_file}")
    endif()
  endforeach()
endforeach()
