foreach(_required TRACELOOM_ANALYZER TRACELOOM_REMOVED_JSON_PATH)
  if(NOT DEFINED ${_required})
    message(FATAL_ERROR "${_required} is required")
  endif()
endforeach()

file(REMOVE "${TRACELOOM_REMOVED_JSON_PATH}")
execute_process(
  COMMAND "${TRACELOOM_ANALYZER}"
    /nonexistent/traceloom-profile.db
    --out "${TRACELOOM_REMOVED_JSON_PATH}"
  RESULT_VARIABLE _result
  OUTPUT_VARIABLE _stdout
  ERROR_VARIABLE _stderr
)

if(_result EQUAL 0)
  message(FATAL_ERROR "removed native JSON --out option was accepted")
endif()

set(_output "${_stdout}\n${_stderr}")
if(NOT _output MATCHES "unknown argument: --out")
  message(FATAL_ERROR
    "removed option failed for the wrong reason: ${_output}")
endif()

if(EXISTS "${TRACELOOM_REMOVED_JSON_PATH}")
  message(FATAL_ERROR "removed native JSON option created an artifact")
endif()
