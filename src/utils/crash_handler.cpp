#include "crash_handler.h"
#include <csignal>
#include <execinfo.h>
#include <unistd.h>
#include <cstdio>
#include <new>
#include <exception>
#include <stdexcept>
#include <cstdlib>

static void skottieCrashHandler(int sig) {
    void* frames[128];
    int n = backtrace(frames, 128);

    const char* name = "UNKNOWN";
    switch (sig) {
        case SIGSEGV: name = "SIGSEGV"; break;
        case SIGABRT: name = "SIGABRT"; break;
        case SIGILL:  name = "SIGILL";  break;
        case SIGFPE:  name = "SIGFPE";  break;
        case SIGBUS:  name = "SIGBUS";  break;
        case SIGTERM: name = "SIGTERM"; break;
        case SIGXCPU: name = "SIGXCPU"; break;
        case SIGXFSZ: name = "SIGXFSZ"; break;
    }

    dprintf(STDERR_FILENO, "[ERROR] Caught signal %d (%s). Backtrace (%d frames):\n", sig, name, n);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    
    // For SIGTERM (often sent before OOM kill), provide helpful message
    if (sig == SIGTERM) {
        dprintf(STDERR_FILENO, "[ERROR] Process terminated (possibly due to OOM). Check system memory limits.\n");
    }
    
    _exit(128 + sig);
}

// Handler for C++ exceptions (catches std::bad_alloc before OOM kill)
static void terminateHandler() {
    void* frames[128];
    int n = backtrace(frames, 128);
    
    dprintf(STDERR_FILENO, "[ERROR] Unhandled C++ exception. Backtrace (%d frames):\n", n);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    
    // Try to get the current exception if available
    auto current_exception = std::current_exception();
    if (current_exception) {
        try {
            std::rethrow_exception(current_exception);
        } catch (const std::bad_alloc& e) {
            dprintf(STDERR_FILENO, "[ERROR] Out of memory: std::bad_alloc caught: %s\n", e.what());
            dprintf(STDERR_FILENO, "[ERROR] This indicates memory allocation failure before OOM killer.\n");
        } catch (const std::exception& e) {
            dprintf(STDERR_FILENO, "[ERROR] Exception: %s\n", e.what());
        } catch (...) {
            dprintf(STDERR_FILENO, "[ERROR] Unknown exception type\n");
        }
    } else {
        dprintf(STDERR_FILENO, "[ERROR] No active exception (std::terminate called directly)\n");
    }
    
    std::abort();
}

void installCrashHandlers() {
    std::signal(SIGSEGV, skottieCrashHandler);
    std::signal(SIGABRT, skottieCrashHandler);
    std::signal(SIGILL,  skottieCrashHandler);
    std::signal(SIGFPE,  skottieCrashHandler);
    std::signal(SIGBUS,  skottieCrashHandler);
    std::signal(SIGTERM, skottieCrashHandler);  // Often sent before OOM kill
    std::signal(SIGXCPU, skottieCrashHandler);   // CPU time limit exceeded
    std::signal(SIGXFSZ, skottieCrashHandler);   // File size limit exceeded
}

void installExceptionHandlers() {
    std::set_terminate(terminateHandler);
}

