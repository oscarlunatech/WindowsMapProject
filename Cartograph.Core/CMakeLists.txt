find_package(boost_geometry REQUIRED CONFIG)
find_package(PROJ CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(Cartograph.Core STATIC
    src/geometry.cpp
    src/dataset.cpp
    src/map.cpp
    src/index/spatial_index.cpp
    src/geom/simplify.cpp
    src/geom/predicates.cpp
    src/query/identify.cpp
    src/render/viewport.cpp
    src/render/renderer.cpp
    src/render/layer_cache.cpp
    src/jobs/thread_pool.cpp
    src/crs/transformer.cpp
    src/style/stylesheet.cpp
    src/style/style_io.cpp
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
# PROJ stays hidden behind crs::Transformer's pimpl (proj.h never appears in
# a public header), so it's a private implementation detail too.
target_link_libraries(Cartograph.Core PRIVATE PROJ::proj)
# Same again for the JSON parser: it's confined to src/style/style_io.cpp, and
# style_spec.h exposes only Cartograph's own StyleSpec types.
target_link_libraries(Cartograph.Core PRIVATE nlohmann_json::nlohmann_json)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
