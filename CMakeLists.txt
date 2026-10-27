cmake_minimum_required(VERSION 3.13)
project(joycond)

set(CMAKE_CXX_STANDARD 17)

# Generate compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

find_package(PkgConfig)
pkg_check_modules(LIBEVDEV REQUIRED libevdev)
pkg_check_modules(LIBUDEV REQUIRED libudev)

add_executable(joycond "")
# Enable verbose debug prints in Debug builds
target_compile_definitions(joycond PRIVATE $<$<CONFIG:Debug>:JOYCOND_DEBUG>)
include_directories(
    include/
    ${LIBEVDEV_INCLUDE_DIRS}
    ${LIBUDEV_INCLUDE_DIRS}
    )
target_link_libraries(
    joycond
    ${LIBEVDEV_LIBRARIES}
    ${LIBUDEV_LIBRARIES}
    )

add_subdirectory(src)

install(TARGETS joycond DESTINATION /usr/bin/
        PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        )
install(FILES udev/89-joycond.rules udev/72-joycond.rules DESTINATION /usr/lib/udev/rules.d/
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ 
        )
install(FILES systemd/joycond.service DESTINATION /usr/lib/systemd/system
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
        )
install(FILES joycond.conf DESTINATION /etc
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
        )
install(FILES data/com.github.mdjou.joycond.metainfo.xml
        DESTINATION /usr/share/metainfo
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
        )
install(FILES doc/joycond.1 DESTINATION /usr/share/man/man1
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
        )


# Removes files listed in install_manifest.txt; works for both in-tree and
# out-of-source builds (the manifest lives in the build directory).
add_custom_target("uninstall" COMMENT "Uninstall installed files")
add_custom_command(
    TARGET "uninstall"
    POST_BUILD
    COMMENT "Uninstall files with install_manifest.txt"
    COMMAND ${CMAKE_COMMAND} -D MANIFEST=${CMAKE_CURRENT_BINARY_DIR}/install_manifest.txt
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake
)