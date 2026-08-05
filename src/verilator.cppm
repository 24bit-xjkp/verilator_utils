module;
#include <verilator_fwd.hpp>
export module verilator_utils:verilator;
import std;
import std.compat;

extern "C++"
{
#if __has_cpp_attribute(clang::type_visibility)
    // libc++下需要设置类的rtti信息可见性为default才能跨DSO使用dynamic_cast
    // 参见https://clang.llvm.org/docs/AttributeReference.html#type-visibility
    class [[clang::type_visibility("default")]] VerilatedVcdC;
    class [[clang::type_visibility("default")]] VerilatedFstC;
    class [[clang::type_visibility("default")]] VerilatedSaifC;
#endif

#include <verilated.h>
#include <verilated_cov.h>
#include <verilated_vcd_c.h>
#include "undefine_verilator_tracer_macros.hpp"
#include <verilated_fst_c.h>
// NOLINTNEXTLINE(readability-duplicate-include)
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
