#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>
import verilator_utils;

int main(int argc, char** argv)
{
    ::verilator_utils::detail::set_console_utf8 _{};
    return ::doctest::Context(argc, argv).run();
}
