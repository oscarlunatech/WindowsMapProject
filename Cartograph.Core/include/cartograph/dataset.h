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
    //
    // Every layer is reprojected to targetCrs (any PROJ-recognized string,
    // e.g. "EPSG:3857") as it's read, so layers from different source CRSs
    // end up aligned in one coordinate space. A layer with no CRS metadata
    // is passed through untransformed - there's no legitimate source to
    // transform from - and reports an empty crsWkt() rather than falsely
    // claiming to be in targetCrs. Throws crs::CrsError if targetCrs can't
    // be parsed.
    static Dataset open(const std::string& path, const std::string& targetCrs);
    static Dataset open(const std::string& path);  // uses defaultDisplayCrs()

    // EPSG:3857 (Web Mercator): what web maps and XYZ basemap tiles use.
    static const char* defaultDisplayCrs();

    const std::vector<Layer>& layers() const { return layers_; }

    // Moves the layers out, leaving this Dataset empty. Map uses this to take
    // ownership at load time - a Dataset is the loader, not the thing that
    // gets drawn, so nothing needs it to stay populated afterwards.
    std::vector<Layer> takeLayers() { return std::move(layers_); }

    // Union of every layer's extent.
    Envelope extent() const;

private:
    explicit Dataset(std::vector<Layer> layers) : layers_(std::move(layers)) {}

    std::vector<Layer> layers_;
};

}  // namespace cartograph
