#ifndef CRASH_HANDLER_H
#define CRASH_HANDLER_H

// Install crash handlers for signals
void installCrashHandlers();

// Install C++ exception handlers (for std::bad_alloc, etc.)
void installExceptionHandlers();

#endif // CRASH_HANDLER_H

