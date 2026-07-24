local verilator_options = {"-Wall", "--trace-fst", {"--x-assign", "unique"}, {"--x-initial", "unique"}, "--coverage"}
set_warnings("none")
add_toolchains("@verilator")

rtl_verilator_target = {
    ["edge_detector"] = "edge_detector",
    ["lfsr_m7"] = "lfsr_m7_wrapper",
}

for name, top_module in pairs(rtl_verilator_target) do
    target(format("unit_test_rtl_%s_verilator", name))
        add_rules("verilator.shared")
        add_deps("verilator_utils_fst")
        add_files(name.."*.sv")
        set_default(false)
        add_values("verilator.flags", table.join(verilator_options, {"--top", top_module}))
        set_policy("build.fence", true)
        on_load(function (target)
            target:set("targetdir", path.join(target:targetdir(), name))
        end)
    target_end()
end
