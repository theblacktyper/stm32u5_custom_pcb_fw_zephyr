# Generate version.h with current build timestamp.
# Runs at build time so each build gets a fresh timestamp.
if(WIN32)
  execute_process(
    COMMAND powershell -NoProfile -Command "Get-Date -Format 'yyyy-MM-dd HH:mm'"
    OUTPUT_VARIABLE BUILD_DATETIME
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
else()
  execute_process(
    COMMAND date "+%Y-%m-%d %H:%M"
    OUTPUT_VARIABLE BUILD_DATETIME
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
endif()
file(WRITE "${OUTPUT_FILE}" "/* Auto-generated at build time - do not edit */\n")
file(APPEND "${OUTPUT_FILE}" "#ifndef BUILD_VERSION_H\n")
file(APPEND "${OUTPUT_FILE}" "#define BUILD_VERSION_H\n")
file(APPEND "${OUTPUT_FILE}" "#define BUILD_DATE_TIME \"${BUILD_DATETIME}\"\n")
file(APPEND "${OUTPUT_FILE}" "#endif\n")
