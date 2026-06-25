local verilator_options = {"-Wall", "--trace-fst", {"--x-assign", "unique"}, {"-x-initial", "unique"}}
add_packages("zlib")
set_warnings("none")
add_toolchains("@verilator")

target("unit_test_rtl_edge_detector_verilator")
    add_rules("verilator.shared")
    add_files("edge_detector.sv")
    set_default(false)
    add_values("verilator.flags", table.join(verilator_options, {"--top", "edge_detector"}))
target_end()
