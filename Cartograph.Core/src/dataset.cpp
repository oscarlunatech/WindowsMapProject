#include "cartograph/dataset.h"

#include <cpl_conv.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <mutex>
#include <optional>

#include "cartograph/crs/transformer.h"

namespace cartograph {

namespace {

// Every layer is normalized to this CRS at load time (see convertLayer),
// so layers from different source CRSs render correctly aligned together
// instead of each staying in its own native coordinate space.
constexpr const char* kTargetCrs = "EPSG:4326";

// proj.db ships next to the running executable (see CMakeLists.txt); PROJ
// doesn't discover it there on its own. std::call_once (rather than a plain
// static bool) matters now that Dataset::open can be called from a
// background loader thread (Phase 5) - a plain bool double-checked-init has
// no memory barrier and would race if two threads ever called open() at
// nearly the same time.
void ensurePlatformSetup() {
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        char exePath[1024];
        if (CPLGetExecPath(exePath, sizeof(exePath))) {
            CPLSetConfigOption("PROJ_DATA", CPLGetDirname(exePath));
        }
        GDALAllRegister();
    });
}

FieldType convertFieldType(OGRFieldType type) {
    switch (type) {
        case OFTInteger:
            return FieldType::Integer;
        case OFTInteger64:
            return FieldType::Integer64;
        case OFTReal:
            return FieldType::Real;
        case OFTString:
            return FieldType::String;
        default:
            return FieldType::Unknown;
    }
}

AttributeValue convertFieldValue(const OGRFeature& feature, int index, FieldType type) {
    if (!feature.IsFieldSetAndNotNull(index)) {
        return std::monostate{};
    }
    switch (type) {
        case FieldType::Integer:
            return static_cast<std::int64_t>(feature.GetFieldAsInteger(index));
        case FieldType::Integer64:
            return static_cast<std::int64_t>(feature.GetFieldAsInteger64(index));
        case FieldType::Real:
            return feature.GetFieldAsDouble(index);
        case FieldType::String:
            return std::string(feature.GetFieldAsString(index));
        default:
            return std::monostate{};
    }
}

GeometryType convertGeometryType(OGRwkbGeometryType type) {
    switch (wkbFlatten(type)) {
        case wkbPoint:
            return GeometryType::Point;
        case wkbLineString:
            return GeometryType::LineString;
        case wkbPolygon:
            return GeometryType::Polygon;
        case wkbMultiPoint:
            return GeometryType::MultiPoint;
        case wkbMultiLineString:
            return GeometryType::MultiLineString;
        case wkbMultiPolygon:
            return GeometryType::MultiPolygon;
        default:
            return GeometryType::Unknown;
    }
}

// transformer is null when the layer had no CRS metadata to reproject from
// (see convertLayer) - points pass through unchanged in that case.
Point2D makePoint(double x, double y, const crs::Transformer* transformer) {
    const Point2D p{x, y};
    return transformer != nullptr ? transformer->transform(p) : p;
}

Ring convertRing(const OGRLinearRing& ring, const crs::Transformer* transformer) {
    Ring result;
    result.reserve(static_cast<std::size_t>(ring.getNumPoints()));
    for (int i = 0; i < ring.getNumPoints(); ++i) {
        result.push_back(makePoint(ring.getX(i), ring.getY(i), transformer));
    }
    return result;
}

Ring convertLine(const OGRLineString& line, const crs::Transformer* transformer) {
    Ring result;
    result.reserve(static_cast<std::size_t>(line.getNumPoints()));
    for (int i = 0; i < line.getNumPoints(); ++i) {
        result.push_back(makePoint(line.getX(i), line.getY(i), transformer));
    }
    return result;
}

Part convertPolygonPart(const OGRPolygon& polygon, const crs::Transformer* transformer) {
    Part part;
    if (const OGRLinearRing* exterior = polygon.getExteriorRing()) {
        part.push_back(convertRing(*exterior, transformer));
    }
    for (int i = 0; i < polygon.getNumInteriorRings(); ++i) {
        part.push_back(convertRing(*polygon.getInteriorRing(i), transformer));
    }
    return part;
}

Geometry convertGeometry(OGRGeometry* geom, const crs::Transformer* transformer) {
    if (geom == nullptr) {
        return Geometry{};
    }

    const GeometryType type = convertGeometryType(geom->getGeometryType());
    std::vector<Part> parts;

    switch (type) {
        case GeometryType::Point: {
            const auto* point = geom->toPoint();
            parts.push_back(Part{Ring{makePoint(point->getX(), point->getY(), transformer)}});
            break;
        }
        case GeometryType::LineString: {
            parts.push_back(Part{convertLine(*geom->toLineString(), transformer)});
            break;
        }
        case GeometryType::Polygon: {
            parts.push_back(convertPolygonPart(*geom->toPolygon(), transformer));
            break;
        }
        case GeometryType::MultiPoint: {
            auto* multi = geom->toMultiPoint();
            for (int i = 0; i < multi->getNumGeometries(); ++i) {
                const auto* point = multi->getGeometryRef(i)->toPoint();
                parts.push_back(Part{Ring{makePoint(point->getX(), point->getY(), transformer)}});
            }
            break;
        }
        case GeometryType::MultiLineString: {
            auto* multi = geom->toMultiLineString();
            for (int i = 0; i < multi->getNumGeometries(); ++i) {
                parts.push_back(Part{convertLine(*multi->getGeometryRef(i)->toLineString(), transformer)});
            }
            break;
        }
        case GeometryType::MultiPolygon: {
            auto* multi = geom->toMultiPolygon();
            for (int i = 0; i < multi->getNumGeometries(); ++i) {
                parts.push_back(convertPolygonPart(*multi->getGeometryRef(i)->toPolygon(), transformer));
            }
            break;
        }
        default:
            break;
    }

    return Geometry{type, std::move(parts)};
}

Layer convertLayer(OGRLayer& ogrLayer) {
    std::vector<FieldDef> fields;
    const OGRFeatureDefn* defn = ogrLayer.GetLayerDefn();
    for (int i = 0; i < defn->GetFieldCount(); ++i) {
        const OGRFieldDefn* fieldDefn = defn->GetFieldDefn(i);
        fields.push_back(FieldDef{fieldDefn->GetNameRef(), convertFieldType(fieldDefn->GetType())});
    }

    // WKT2 (not exportToPrettyWkt's default WKT1) - PROJ's own native,
    // unambiguous format. Shapefiles' .prj sidecars are very often
    // ESRI-flavored WKT1 (e.g. "GCS_WGS_1984"/"D_WGS_1984" naming), which
    // GDAL parses fine but raw PROJ's proj_create() (used by
    // crs::Transformer below) can reject outright - WKT2 round-trips
    // cleanly through PROJ regardless of the source .prj's original dialect.
    std::string sourceCrsWkt;
    if (const OGRSpatialReference* srs = ogrLayer.GetSpatialRef()) {
        const char* const wktOptions[] = {"FORMAT=WKT2", nullptr};
        sourceCrsWkt = srs->exportToWkt(wktOptions);
    }

    // No transformer (geometry passes through unchanged) when the layer has
    // no CRS metadata at all - there's no legitimate source CRS to
    // transform from.
    std::optional<crs::Transformer> transformer;
    if (!sourceCrsWkt.empty()) {
        transformer.emplace(sourceCrsWkt, kTargetCrs);
    }
    const crs::Transformer* transformerPtr = transformer ? &*transformer : nullptr;

    std::vector<Feature> features;
    ogrLayer.ResetReading();
    for (auto& ogrFeature : ogrLayer) {
        std::vector<AttributeValue> attributes;
        attributes.reserve(fields.size());
        for (std::size_t i = 0; i < fields.size(); ++i) {
            attributes.push_back(convertFieldValue(*ogrFeature, static_cast<int>(i), fields[i].type));
        }
        features.emplace_back(ogrFeature->GetFID(), convertGeometry(ogrFeature->GetGeometryRef(), transformerPtr),
                               std::move(attributes));
    }

    // Computed from the (already-reprojected) features rather than
    // ogrLayer.GetExtent()'s native-CRS bbox, which would be wrong once
    // geometry has moved to kTargetCrs.
    Envelope extent;
    for (const Feature& feature : features) {
        extent.expand(feature.geometry().extent());
    }

    // Reports the CRS the coordinates are actually in: kTargetCrs if a
    // transform was applied, or empty if the layer had no CRS metadata to
    // begin with (honest "unknown", not a false claim it's in kTargetCrs).
    const std::string crsWkt = transformerPtr != nullptr ? kTargetCrs : std::string();

    return Layer{ogrLayer.GetName(), std::move(fields), std::move(features), extent, crsWkt};
}

}  // namespace

Dataset Dataset::open(const std::string& path) {
    ensurePlatformSetup();

    auto* gdalDataset =
        static_cast<GDALDataset*>(GDALOpenEx(path.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
    if (gdalDataset == nullptr) {
        throw DatasetOpenError("failed to open '" + path + "'");
    }

    std::vector<Layer> layers;
    for (OGRLayer* ogrLayer : gdalDataset->GetLayers()) {
        layers.push_back(convertLayer(*ogrLayer));
    }

    GDALClose(gdalDataset);
    return Dataset{std::move(layers)};
}

Envelope Dataset::extent() const {
    Envelope extent;
    for (const Layer& layer : layers_) {
        extent.expand(layer.extent());
    }
    return extent;
}

}  // namespace cartograph
