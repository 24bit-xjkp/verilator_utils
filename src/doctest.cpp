module;
#include "doctest_fwd.hpp"
export module verilator_utils.doctest;
import std;
import std.compat;
export extern "C++"
{
#include <doctest.h>
}
