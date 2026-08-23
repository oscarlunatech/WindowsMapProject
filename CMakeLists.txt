cmake_minimum_required(VERSION 3.25)
project(Cartograph LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

find_package(GDAL CONFIG REQUIRED)

add_subdirectory(Cartograph.Core)

add_executable(cartograph_cli src/main.cpp src/viewer.cpp)
target_link_libraries(cartograph_cli PRIVATE Cartograph::Core d2d1 dwrite user32)
# viewer.cpp calls the explicit wide (W) Win32 APIs throughout; UNICODE/_UNICODE
# make macros like IDC_ARROW resolve to their wide-string form to match.
target_compile_definitions(cartograph_cli PRIVATE UNICODE _UNICODE)

# PROJ needs proj.db at runtime for coordinate transforms; it doesn't get
# copied alongside the DLLs by vcpkg's applocal step like the binaries do.
add_custom_command(TARGET cartograph_cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/proj/proj.db"
        "$<TARGET_FILE_DIR:cartograph_cli>/proj.db"
)
