#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-W#warnings"
#endif
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>
#ifdef __clang__
    #pragma clang diagnostic pop
#endif

namespace verilator_utils::detail
{
    extern "C++" int argc;
    extern "C++" const char** argv;
}  // namespace verilator_utils::detail

int main(int argc, const char* argv[])
{
    ::verilator_utils::detail::argc = argc;
    ::verilator_utils::detail::argv = argv;
    return ::doctest::Context{argc, argv}.run();
}
