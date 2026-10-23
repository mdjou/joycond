# Remove the files CMake recorded in an install manifest (install_manifest.txt).
# Used by the `uninstall` custom target in CMakeLists.txt:
#
#   cmake -D MANIFEST=<path to install_manifest.txt> -P cmake_uninstall.cmake

if(NOT DEFINED MANIFEST)
    message(FATAL_ERROR "MANIFEST must be set to the path of install_manifest.txt")
endif()

if(NOT EXISTS "${MANIFEST}")
    message(STATUS "No install manifest at ${MANIFEST}; nothing to uninstall")
    return()
endif()

file(STRINGS "${MANIFEST}" FILES)
foreach(FILE IN LISTS FILES)
    execute_process(COMMAND rm -vf "${FILE}")
endforeach()
