module;
#include <verilator_fwd.hpp>
export module unit_test_rtl.counter;
import std;
import std.compat;

extern "C++"
{
#include <unit_test_rtl_counter_verilator.h>
}

#include <verilator_bwd.hpp>
