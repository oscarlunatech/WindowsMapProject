find_package(Catch2 CONFIG REQUIRED)

add_executable(cartograph_core_tests
    test_geometry.cpp
    test_dataset.cpp
    test_viewport.cpp
    test_render.cpp
    test_spatial_index.cpp
    test_simplify.cpp
    test_layer_cache.cpp
    test_thread_pool.cpp
)

target_link_libraries(cartograph_core_tests PRIVATE Cartograph::Core Catch2::Catch2WithMain)
target_compile_definitions(cartograph_core_tests PRIVATE
    CARTOGRAPH_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

include(Catch)
catch_discover_tests(cartograph_core_tests)

# proj.db must sit next to this binary too (see Cartograph.Core/src/dataset.cpp).
add_custom_command(TARGET cartograph_core_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/proj/proj.db"
        "$<TARGET_FILE_DIR:cartograph_core_tests>/proj.db"
)
