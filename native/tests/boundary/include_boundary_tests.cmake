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
