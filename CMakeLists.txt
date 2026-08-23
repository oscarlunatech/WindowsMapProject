cmake_minimum_required(VERSION 3.25)
project(Cartograph LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(GDAL CONFIG REQUIRED)

add_executable(cartograph_cli src/main.cpp)
target_link_libraries(cartograph_cli PRIVATE GDAL::GDAL)

# PROJ needs proj.db at runtime for coordinate transforms; it doesn't get
# copied alongside the DLLs by vcpkg's applocal step like the binaries do.
add_custom_command(TARGET cartograph_cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/proj/proj.db"
        "$<TARGET_FILE_DIR:cartograph_cli>/proj.db"
)
