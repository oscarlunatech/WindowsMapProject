#include "gdal_platform.h"

#include <cpl_conv.h>
#include <gdal_priv.h>

#include <mutex>

namespace cartograph::detail {

void ensureGdalPlatformSetup() {
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        char exePath[1024];
        if (CPLGetExecPath(exePath, sizeof(exePath))) {
            CPLSetConfigOption("PROJ_DATA", CPLGetDirname(exePath));
        }
        GDALAllRegister();
    });
}

}  // namespace cartograph::detail
