#pragma once

#include <string>
#include <utility>
#include <vector>

#include "cartograph/feature.h"
#include "cartograph/geometry.h"

namespace cartograph {

enum class FieldType { Integer, Integer64, Real, String, Unknown };

struct FieldDef {
    std::string name;
    FieldType type;
};

class Layer {
public:
    Layer(std::string name, std::vector<FieldDef> fields, std::vector<Feature> features,
          Envelope extent, std::string crsWkt)
        : name_(std::move(name)),
          fields_(std::move(fields)),
          features_(std::move(features)),
          extent_(extent),
          crsWkt_(std::move(crsWkt)) {}

    const std::string& name() const { return name_; }
    const std::vector<FieldDef>& fields() const { return fields_; }
    const std::vector<Feature>& features() const { return features_; }
    const Envelope& extent() const { return extent_; }

    // The CRS this layer's geometry is actually in - not necessarily the
    // source file's original CRS. Dataset::open() reprojects every layer to
    // one common target CRS at load time (see dataset.cpp), so this reports
    // that target CRS if a transform was applied, or is empty if the source
    // file had no CRS metadata to reproject from in the first place.
    const std::string& crsWkt() const { return crsWkt_; }

private:
    std::string name_;
    std::vector<FieldDef> fields_;
    std::vector<Feature> features_;
    Envelope extent_;
    std::string crsWkt_;
};

}  // namespace cartograph
