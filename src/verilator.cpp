module;
#include <verilator_fwd.hpp>
export module verilator_utils:verilator;
import std;
import std.compat;

extern "C++"
{
#include <verilated.h>
#include <verilated_cov.h>
#include <verilated_vcd_c.h>
#include "undefine_verilator_tracer_macros.hpp"
#include <verilated_fst_c.h>
#include "undefine_verilator_tracer_macros.hpp"
#include <verilated_saif_c.h>
}

#include <verilator_bwd.hpp>

export namespace verilator_utils::verilator
{
    using ::CData;
    using ::EData;
    using ::IData;
    using ::IsVlUnpacked;
    using ::QData;
    using ::SData;
    using ::VerilatedContext;
    using ::VerilatedCovContext;
    using ::VerilatedFstC;
    using ::VerilatedModel;
    using ::VerilatedSaifC;
    using ::VerilatedVcdC;
    using ::VlIsVlWide;
    using ::VlUnpacked;
    using ::VlWide;
}  // namespace verilator_utils::verilator
