# CPack pre-build script (macOS): ad-hoc signs the staged programs.
#
# On Apple Silicon every executable must carry a valid code signature. The
# linker signs ad hoc, but stripping (CPACK_STRIP_FILES) rewrites the binaries
# and invalidates that signature; Gatekeeper then reports the downloaded app as
# "damaged". Re-signing after the staging install fixes this. The signature is
# ad hoc ("-"), so the app is still unnotarized and must be opened once with
# right click > Open.

file(GLOB_RECURSE staged_bundles LIST_DIRECTORIES true "${CPACK_TEMPORARY_INSTALL_DIRECTORY}/*.app")
list(FILTER staged_bundles INCLUDE REGEX "\\.app$")
# The command-line tools beside the main program in Contents/MacOS (or in
# tools/ without a bundle): nested code is signed before its bundle.
file(GLOB_RECURSE staged_tools "${CPACK_TEMPORARY_INSTALL_DIRECTORY}/*")
list(FILTER staged_tools INCLUDE REGEX "(/tools/[^/]+|\\.app/Contents/MacOS/[^/]+)$")
list(FILTER staged_tools EXCLUDE REGEX "/Contents/MacOS/OpenDay$")

foreach(item IN LISTS staged_tools staged_bundles)
    execute_process(COMMAND codesign --force --sign - --timestamp=none "${item}"
                    RESULT_VARIABLE sign_result)
    if(NOT sign_result EQUAL 0)
        message(FATAL_ERROR "codesign failed for ${item}")
    endif()
    execute_process(COMMAND codesign --verify --strict "${item}" RESULT_VARIABLE verify_result)
    if(NOT verify_result EQUAL 0)
        message(FATAL_ERROR "codesign verification failed for ${item}")
    endif()
    message(STATUS "Ad-hoc signed ${item}")
endforeach()

if(NOT staged_bundles)
    message(FATAL_ERROR "No .app bundle found in ${CPACK_TEMPORARY_INSTALL_DIRECTORY}")
endif()
