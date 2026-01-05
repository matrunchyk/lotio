#include "version.h"

const char* getLotioVersion() {
    #ifdef VERSION
    // VERSION is defined at compile time (from CI/CD or build script)
    return VERSION;
    #else
    // Fallback: use build-time date/time macros (compile-time constants)
    // Note: This fallback should rarely be used as build scripts always define VERSION
    // Format: "dev-MMM DD YYYY-HH:MM:SS" (e.g., "dev-Jan 01 2024-12:00:00")
    // __DATE__ format: "MMM DD YYYY" (e.g., "Jan 01 2024")
    // __TIME__ format: "HH:MM:SS" (e.g., "12:00:00")
    // Note: Build scripts generate "dev-YYYYMMDD-HHMMSS" format when VERSION is not set
    return "dev-" __DATE__ "-" __TIME__;
    #endif
}

