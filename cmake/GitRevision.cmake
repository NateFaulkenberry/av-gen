# Script mode. Writes ${OUT} from ${TEMPLATE}, with the current git revision in it.
#
# Runs on every build (see the custom target in src/CMakeLists.txt). `configure_file` only rewrites
# the file when its contents change, so this does not rebuild the world for nothing -- only when
# the revision actually moves, which is exactly when a benchmark record's conditions have changed.
find_package(Git QUIET)
set(AVGEN_GIT_REVISION "unknown")
set(AVGEN_GIT_DIRTY 0)
set(AVGEN_GIT_BRANCH "unknown")
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE rev
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE revStatus)
    if(revStatus EQUAL 0 AND rev)
        set(AVGEN_GIT_REVISION "${rev}")
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE branch
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE branchStatus)
    if(branchStatus EQUAL 0 AND branch)
        set(AVGEN_GIT_BRANCH "${branch}")
    endif()
    # Tracked files only. Untracked files do not change what was compiled, and treating them as
    # dirty would mark every build that happens to have a scratch file beside it.
    execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
        WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE dirty
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT dirty STREQUAL "")
        set(AVGEN_GIT_DIRTY 1)
    endif()
endif()
configure_file("${TEMPLATE}" "${OUT}" @ONLY)
