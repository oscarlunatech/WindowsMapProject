add_library(Cartograph.Core STATIC
    src/geometry.cpp
    src/dataset.cpp
)
add_library(Cartograph::Core ALIAS Cartograph.Core)

target_include_directories(Cartograph.Core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(Cartograph.Core PUBLIC cxx_std_20)
target_link_libraries(Cartograph.Core PUBLIC GDAL::GDAL)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
