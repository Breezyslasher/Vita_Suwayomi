if(NOT DEFINED SRC_DIR OR NOT DEFINED DST_DIR)
    message(FATAL_ERROR "CopyDirBestEffort.cmake requires -DSRC_DIR and -DDST_DIR")
endif()

if(NOT EXISTS "${SRC_DIR}")
    message(WARNING "Resource source directory not found: ${SRC_DIR}")
    return()
endif()

file(MAKE_DIRECTORY "${DST_DIR}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SRC_DIR}" "${DST_DIR}"
    RESULT_VARIABLE COPY_RESULT
    OUTPUT_VARIABLE COPY_STDOUT
    ERROR_VARIABLE COPY_STDERR
)

if(NOT COPY_RESULT EQUAL 0)
    message(WARNING
        "Best-effort copy failed from '${SRC_DIR}' to '${DST_DIR}' (exit ${COPY_RESULT}). "
        "stdout='${COPY_STDOUT}' stderr='${COPY_STDERR}'"
    )
endif()
