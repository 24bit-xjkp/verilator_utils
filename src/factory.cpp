import std;
import std.compat;
import verilator_utils;

namespace verilator_utils::detail
{
    using namespace ::verilator_utils::verilator;

#ifdef USE_VCD
    [[gnu::visibility("default")]] ::std::unique_ptr<VerilatedVcdC> create_tracer_vcd()
    { return ::std::make_unique<VerilatedVcdC>(); }
#endif

#ifdef USE_FST
    [[gnu::visibility("default")]] ::std::unique_ptr<VerilatedFstC> create_tracer_fst()
    { return ::std::make_unique<VerilatedFstC>(); }
#endif

#ifdef USE_SAIF
    [[gnu::visibility("default")]] ::std::unique_ptr<VerilatedSaifC> create_tracer_saif()
    { return ::std::make_unique<VerilatedSaifC>(); }
#endif
}  // namespace verilator_utils::detail
