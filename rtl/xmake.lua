local verilator_options = {"-Wall", "--trace-fst", {"--x-assign", "unique"}, {"-x-initial", "unique"}}
set_warnings("none")
add_toolchains("@verilator")

target("unit_test_rtl_edge_detector_verilator")
    add_rules("verilator.shared")
    add_deps("verilator_utils_fst")
    add_files("edge_detector.sv")
    set_default(false)
    add_values("verilator.flags", table.join(verilator_options, {"--top", "edge_detector"}))
    set_policy("build.fence", true)
target_end()
