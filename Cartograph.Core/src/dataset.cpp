#include "cartograph/dataset.h"

#include <cpl_conv.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <mutex>

namespace cartograph {

namespace {

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

Ring convertRing(const OGRLinearRing& ring) {
    Ring result;
    result.reserve(static_cast<std::size_t>(ring.getNumPoints()));
    for (int i = 0; i < ring.getNumPoints(); ++i) {
        result.push_back(Point2D{ring.getX(i), ring.getY(i)});
    }
    return result;
}

Ring convertLine(const OGRLineString& line) {
    Ring result;
    result.reserve(static_cast<std::size_t>(line.getNumPoints()));
    for (int i = 0; i < line.getNumPoints(); ++i) {
        result.push_back(Point2D{line.getX(i), line.getY(i)});
    }
    return result;
}

Part convertPolygonPart(const OGRPolygon& polygon) {
    Part part;
    if (const OGRLinearRing* exterior = polygon.getExteriorRing()) {
        part.push_back(convertRing(*exterior));
    }
    for (int i = 0; i < polygon.getNumInteriorRings(); ++i) {
        part.push_back(convertRing(*polygon.getInteriorRing(i)));
    }
    return part;
}

Geometry convertGeometry(OGRGeometry* geom) {
    if (geom == nullptr) {
        return Geometry{};
    }

    const GeometryType type = convertGeometryType(geom->getGeometryType());
    std::vector<Part> parts;

    switch (type) {
        case GeometryType::Point: {
            const auto* point = geom->toPoint();
            parts.push_back(Part{Ring{Point2D{point->getX(), point->getY()}}});
            break;
        }
        case GeometryType::LineString: {
            parts.push_back(Part{convertLine(*geom->toLineString())});
            break;
        }
        case GeometryType::Polygon: {
            parts.push_back(convertPolygonPart(*geom->toPolygon()));
            break;
        }
        case GeometryType::MultiPoint: {
            auto* multi = geom->toMultiPoint();
            for (int i = 0; i < multi->getNumGeometries(); ++i) {
                const auto* point = multi->getGeometryRef(i)->toPoint();
                parts.push_back(Part{Ring{Point2D{point->getX(), point->getY()}}});
            }
            break;
        }
        case GeometryType::MultiLineString: {
            auto* multi = geom->toMultiLineString();
            for (int i = 0; i < multi->getNumGeometries(); ++i) {
                parts.push_back(Part{convertLine(*multi->getGeometryRef(i)->toLineString())});
            }
            break;
        }
        case GeometryType::MultiPolygon: {
            auto* multi = geom->toMultiPolygon();
            for (int i = 0; i < multi->getNumGeometries(); ++i) {
                parts.push_back(convertPolygonPart(*multi->getGeometryRef(i)->toPolygon()));
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

    std::vector<Feature> features;
    ogrLayer.ResetReading();
    for (auto& ogrFeature : ogrLayer) {
        std::vector<AttributeValue> attributes;
        attributes.reserve(fields.size());
        for (std::size_t i = 0; i < fields.size(); ++i) {
            attributes.push_back(convertFieldValue(*ogrFeature, static_cast<int>(i), fields[i].type));
        }
        features.emplace_back(ogrFeature->GetFID(), convertGeometry(ogrFeature->GetGeometryRef()),
                               std::move(attributes));
    }

    Envelope extent;
    OGREnvelope ogrExtent;
    if (ogrLayer.GetExtent(&ogrExtent) == OGRERR_NONE) {
        extent.expand(Point2D{ogrExtent.MinX, ogrExtent.MinY});
        extent.expand(Point2D{ogrExtent.MaxX, ogrExtent.MaxY});
    }

    std::string crsWkt;
    if (const OGRSpatialReference* srs = ogrLayer.GetSpatialRef()) {
        char* wkt = nullptr;
        srs->exportToPrettyWkt(&wkt);
        if (wkt != nullptr) {
            crsWkt = wkt;
            CPLFree(wkt);
        }
    }

    return Layer{ogrLayer.GetName(), std::move(fields), std::move(features), extent, std::move(crsWkt)};
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
