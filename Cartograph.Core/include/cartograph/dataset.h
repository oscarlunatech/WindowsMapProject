#pragma once

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "cartograph/layer.h"

namespace cartograph {

class DatasetOpenError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Dataset {
public:
    // Opens path via GDAL/OGR and converts every layer into Cartograph's own
    // types; no GDAL/OGR object survives past this call. Throws
    // DatasetOpenError if the dataset can't be opened.
    static Dataset open(const std::string& path);

    const std::vector<Layer>& layers() const { return layers_; }

    // Union of every layer's extent.
    Envelope extent() const;

private:
    explicit Dataset(std::vector<Layer> layers) : layers_(std::move(layers)) {}

    std::vector<Layer> layers_;
};

}  // namespace cartograph
