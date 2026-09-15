execute_process(COMMAND "${ANALYZER}" "${PROFILE}" --rules-config "${CONFIG}"
  --structural-order host-launch --threads 2 --output "${OUTPUT}"
  RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "rules-config analysis failed: ${stderr}")
endif()
execute_process(COMMAND "${CHECKER}" "${OUTPUT}" rules-config RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "rules-config provenance/sidecar check failed")
endif()
foreach(flag --symbol-rules --extend-symbol-rules --classification-rules --extend-classification-rules
    --event-reconciliation-rules --extend-event-reconciliation-rules --classification-rule-override)
  execute_process(COMMAND "${ANALYZER}" "${PROFILE}" --rules-config "${CONFIG}" "${flag}" unused
    RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 10)
  if(result EQUAL 0 OR NOT stderr MATCHES "cannot be mixed")
    message(FATAL_ERROR "did not reject mixed ${flag}: ${stderr}")
  endif()
endforeach()
# Parse the valid legacy document, then reject conflicting entry points.
file(WRITE "${OUTPUT}.macro.yaml" "schema: traceloom-match-rules-v1\nordered_markers: []\n")
execute_process(COMMAND "${ANALYZER}" "${PROFILE}" --match-rules "${OUTPUT}.macro.yaml"
  --rules-config "${CONFIG}" RESULT_VARIABLE result ERROR_VARIABLE stderr TIMEOUT 10)
if(result EQUAL 0 OR NOT stderr MATCHES "cannot be mixed")
  message(FATAL_ERROR "did not reject legacy macro flag: ${stderr}")
endif()
file(REMOVE "${OUTPUT}.macro.yaml")

execute_process(COMMAND "${ANALYZER}" "${PROFILE}" --rules-config "${EMPTY_CONFIG}"
  --threads 2 --output "${OUTPUT}.empty.db"
  RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "empty replacement analysis failed: ${stderr}")
endif()
execute_process(COMMAND "${CHECKER}" "${OUTPUT}.empty.db" empty-classification RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "empty replacement silently reloaded default policy")
endif()
