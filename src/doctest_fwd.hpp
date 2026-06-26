#pragma once
#if defined(__APPLE__)
    // Apple detection taken from Catch2 codebase
    // For <TargetConditionals.h> information:
    //   https://github.com/swiftlang/swift-corelibs-foundation/blob/release/5.10/CoreFoundation/Base.subproj/SwiftRuntime/TargetConditionals.h
    #include <TargetConditionals.h>
    #if (defined(TARGET_OS_MAC) && TARGET_OS_MAC == 1) || (defined(TARGET_OS_OSX) && TARGET_OS_OSX == 1)
        #define DOCTEST_PLATFORM_MAC

    #elif defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE == 1
        #define DOCTEST_PLATFORM_IPHONE
    #endif

#elif defined(WIN32) || defined(_WIN32)
    #define DOCTEST_PLATFORM_WINDOWS

#elif defined(__wasi__)
    #define DOCTEST_PLATFORM_WASI

#else  // defined(linux) || defined(__linux) // defined(__linux__)
    #define DOCTEST_PLATFORM_LINUX
#endif  // DOCTEST_PLATFORM

#ifdef DOCTEST_PLATFORM_MAC
    #include <sys/types.h>
    #include <unistd.h>
    #include <sys/sysctl.h>
#endif  // DOCTEST_PLATFORM_MAC

#ifdef DOCTEST_PLATFORM_WINDOWS

    // defines for a leaner windows.h
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
        #define DOCTEST_UNDEF_WIN32_LEAN_AND_MEAN
    #endif  // WIN32_LEAN_AND_MEAN
    #ifndef NOMINMAX
        #define NOMINMAX
        #define DOCTEST_UNDEF_NOMINMAX
    #endif  // NOMINMAX

    // not sure what AfxWin.h is for - here I do what Catch does
    #ifdef __AFXDLL
        #include <AfxWin.h>
    #else
        #include <windows.h>
    #endif
    #include <io.h>

    #ifdef DOCTEST_UNDEF_WIN32_LEAN_AND_MEAN
        #undef WIN32_LEAN_AND_MEAN
        #undef DOCTEST_UNDEF_WIN32_LEAN_AND_MEAN
    #endif  // DOCTEST_UNDEF_WIN32_LEAN_AND_MEAN
    #ifdef DOCTEST_UNDEF_NOMINMAX
        #undef NOMINMAX
        #undef DOCTEST_UNDEF_NOMINMAX
    #endif  // DOCTEST_UNDEF_NOMINMAX

#else  // DOCTEST_PLATFORM_WINDOWS

    #include <sys/time.h>
    #include <unistd.h>

#endif  // DOCTEST_PLATFORM_WINDOWS

#undef DOCTEST_PLATFORM_MAC
#undef DOCTEST_PLATFORM_IPHONE
#undef DOCTEST_PLATFORM_WINDOWS
#undef DOCTEST_PLATFORM_WASI
#undef DOCTEST_PLATFORM_LINUX

#ifdef __clang__
#pragma clang diagnostic ignored "-Winclude-angled-in-module-purview"
#endif
#include <clear_all_cpp_std_headers.h>
