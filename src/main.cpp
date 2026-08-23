#include <format>
#include <iostream>

#include <cpl_conv.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << std::format("usage: {} <path-to-vector-dataset>\n", argv[0]);
        return 1;
    }

    // proj.db ships next to the executable (see CMakeLists.txt); PROJ doesn't
    // discover it there on its own.
    char exePath[1024];
    if (CPLGetExecPath(exePath, sizeof(exePath))) {
        CPLSetConfigOption("PROJ_DATA", CPLGetDirname(exePath));
    }

    GDALAllRegister();

    GDALDataset* dataset = static_cast<GDALDataset*>(
        GDALOpenEx(argv[1], GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
    if (dataset == nullptr) {
        std::cerr << std::format("failed to open '{}'\n", argv[1]);
        return 1;
    }

    for (OGRLayer* layer : dataset->GetLayers()) {
        OGREnvelope extent;
        if (layer->GetExtent(&extent) != OGRERR_NONE) {
            extent = {};
        }

        const OGRSpatialReference* srs = layer->GetSpatialRef();
        char* wkt = nullptr;
        if (srs != nullptr) {
            srs->exportToPrettyWkt(&wkt);
        }

        std::cout << std::format(
            "layer: {}\n"
            "  features: {}\n"
            "  extent: [{}, {}] x [{}, {}]\n"
            "  crs: {}\n",
            layer->GetName(),
            layer->GetFeatureCount(),
            extent.MinX, extent.MaxX, extent.MinY, extent.MaxY,
            wkt != nullptr ? wkt : "(none)");

        if (wkt != nullptr) {
            CPLFree(wkt);
        }
    }

    GDALClose(dataset);
    return 0;
}
