#include <cstdlib>
#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "cartograph/dataset.h"

using namespace cartograph;

namespace {

std::string formatAttribute(const AttributeValue& value) {
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return "(null)";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return v;
            } else {
                return std::format("{}", v);
            }
        },
        value);
}

int runInfo(const std::string& path) {
    const Dataset dataset = Dataset::open(path);
    for (const Layer& layer : dataset.layers()) {
        const Envelope& extent = layer.extent();
        std::cout << std::format(
            "layer: {}\n"
            "  features: {}\n"
            "  extent: [{}, {}] x [{}, {}]\n"
            "  crs: {}\n",
            layer.name(), layer.features().size(), extent.minX, extent.maxX, extent.minY,
            extent.maxY, layer.crsWkt().empty() ? "(none)" : layer.crsWkt());
    }
    return 0;
}

int runDump(const std::string& path, std::size_t limit) {
    const Dataset dataset = Dataset::open(path);
    for (const Layer& layer : dataset.layers()) {
        std::cout << std::format("layer: {}\n", layer.name());
        const auto& fields = layer.fields();

        std::size_t count = 0;
        for (const Feature& feature : layer.features()) {
            if (count >= limit) {
                break;
            }
            std::cout << std::format("  feature {}:\n", feature.id());
            const auto& attributes = feature.attributes();
            for (std::size_t i = 0; i < fields.size(); ++i) {
                std::cout << std::format("    {}: {}\n", fields[i].name, formatAttribute(attributes[i]));
            }
            ++count;
        }
    }
    return 0;
}

void printUsage(const char* argv0) {
    std::cerr << std::format(
        "usage: {} info <path>\n"
        "       {} dump <path> [--limit N]\n",
        argv0, argv0);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    const std::string command = argv[1];
    const std::string path = argv[2];

    try {
        if (command == "info") {
            return runInfo(path);
        }
        if (command == "dump") {
            std::size_t limit = 10;
            for (int i = 3; i < argc; ++i) {
                const std::string_view arg = argv[i];
                if (arg == "--limit" && i + 1 < argc) {
                    limit = static_cast<std::size_t>(std::strtoul(argv[++i], nullptr, 10));
                }
            }
            return runDump(path, limit);
        }
    } catch (const DatasetOpenError& e) {
        std::cerr << std::format("error: {}\n", e.what());
        return 1;
    }

    printUsage(argv[0]);
    return 1;
}
