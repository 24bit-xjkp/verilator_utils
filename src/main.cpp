#include "doctest_fwd.hpp"
import verilator_utils.full;
#include <doctest.h>

int main(int argc, const char* argv[])
{
    ::verilator_utils::detail::set_console_utf8 _{};
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
