#ifdef __clang__
    #pragma clang diagnostic ignored "-W#warnings"
#endif

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>
import std;
import verilator_utils;

int main(int argc, const char* argv[])
{
    ::verilator_utils::detail::argc = argc;
    ::verilator_utils::detail::argv = argv;
    try
    {
        return ::doctest::Context{argc, argv}.run();
    }
    catch(...)
    {
        ::std::terminate();
    }
}
