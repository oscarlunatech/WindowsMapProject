#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "cartograph/geometry.h"

namespace cartograph {

using AttributeValue = std::variant<std::monostate, std::int64_t, double, std::string>;

class Feature {
public:
    Feature(std::int64_t id, Geometry geometry, std::vector<AttributeValue> attributes)
        : id_(id), geometry_(std::move(geometry)), attributes_(std::move(attributes)) {}

    std::int64_t id() const { return id_; }
    const Geometry& geometry() const { return geometry_; }
    const std::vector<AttributeValue>& attributes() const { return attributes_; }

private:
    std::int64_t id_;
    Geometry geometry_;
    std::vector<AttributeValue> attributes_;
};

}  // namespace cartograph
