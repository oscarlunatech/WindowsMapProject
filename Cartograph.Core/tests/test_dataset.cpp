#include <catch2/catch_test_macros.hpp>

#include "cartograph/dataset.h"

using namespace cartograph;

namespace {
std::string fixturePath(const std::string& name) {
    return std::string(CARTOGRAPH_TEST_FIXTURES_DIR) + "/" + name;
}
}  // namespace

TEST_CASE("Opening a nonexistent dataset throws", "[dataset]") {
    REQUIRE_THROWS_AS(Dataset::open(fixturePath("does_not_exist.shp")), DatasetOpenError);
}

TEST_CASE("Natural Earth countries fixture loads correctly", "[dataset]") {
    const Dataset dataset = Dataset::open(fixturePath("ne_110m_admin_0_countries.shp"));

    REQUIRE(dataset.layers().size() == 1);
    const Layer& layer = dataset.layers().front();

    REQUIRE(layer.features().size() > 150);  // ~177 countries at 110m scale
    REQUIRE(layer.extent().valid);
    REQUIRE_FALSE(layer.crsWkt().empty());
    REQUIRE_FALSE(layer.fields().empty());

    for (const Feature& feature : layer.features()) {
        REQUIRE(feature.geometry().type() != GeometryType::Unknown);
    }
}
