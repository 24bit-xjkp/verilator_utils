// NOLINTBEGIN(modernize-deprecated-headers)
// 允许通过宏定义使该头文件失效
#ifndef VERILATOR_FWD_H
#define VERILATOR_FWD_H
#include <string.h>
#include <cassert>
#ifdef __CYGWIN__
    #include <sys/types.h>
    #include <unistd.h>
#else
    #include <inttypes.h>
    #include <sys/types.h>
    #include <unistd.h>
#endif

#include <clear_all_cpp_std_headers.h>
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Winclude-angled-in-module-purview"
    #pragma clang diagnostic ignored "-WTU-local-entity-exposure"
    #pragma clang diagnostic ignored "-Wunused-but-set-variable"
    #pragma clang diagnostic ignored "-Wunused-variable"
    #pragma clang diagnostic ignored "-Wdeprecated-missing-comma-variadic-parameter"
    #pragma clang diagnostic ignored "-Wunused-parameter"
    #pragma clang diagnostic ignored "-Wsign-compare"
#endif
#endif
// NOLINTEND(modernize-deprecated-headers)
