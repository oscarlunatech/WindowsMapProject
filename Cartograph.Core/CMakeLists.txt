find_package(boost_geometry REQUIRED CONFIG)

add_library(Cartograph.Core STATIC
    src/geometry.cpp
    src/dataset.cpp
    src/index/spatial_index.cpp
    src/geom/simplify.cpp
    src/render/viewport.cpp
    src/render/renderer.cpp
    src/render/layer_cache.cpp
    src/jobs/thread_pool.cpp
)
add_library(Cartograph::Core ALIAS Cartograph.Core)

target_include_directories(Cartograph.Core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(Cartograph.Core PUBLIC cxx_std_20)
target_link_libraries(Cartograph.Core PUBLIC GDAL::GDAL d2d1 windowscodecs ole32 uuid)
# Boost.Geometry (including index::rtree) is header-only and stays hidden
# behind SpatialIndex's pimpl, so it's a private implementation detail.
target_link_libraries(Cartograph.Core PRIVATE Boost::geometry)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
