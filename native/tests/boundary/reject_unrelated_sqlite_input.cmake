foreach(_required TRACELOOM_ANALYZER TRACELOOM_UNRELATED_DB)
  if(NOT DEFINED ${_required})
    message(FATAL_ERROR "${_required} is required")
  endif()
endforeach()

execute_process(
  COMMAND "${TRACELOOM_ANALYZER}"
    "${TRACELOOM_UNRELATED_DB}"
    --source-kind ascend_sqlite_hot_path
  RESULT_VARIABLE _result
  OUTPUT_VARIABLE _stdout
  ERROR_VARIABLE _stderr
)

if(_result EQUAL 0)
  message(FATAL_ERROR "unrelated SQLite input was accepted")
endif()

set(_output "${_stdout}\n${_stderr}")
if(NOT _output MATCHES "input is not a supported Ascend/Hygon/CUDA profile DB or directory")
  message(FATAL_ERROR
    "unrelated SQLite input failed for the wrong reason: ${_output}")
endif()
