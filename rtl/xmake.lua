local verilator_options = {
    "-Wall",
    "--trace-fst",
    {"--x-assign", "unique"},
    {"--x-initial", "unique"},
    "--coverage",
    "-DSIMULATION=1",
    "-Irtl"
}
set_warnings("none")
add_toolchains("@verilator")

rtl_verilator_target = {
    ["edge_detector"] = {},
    ["lfsr_m7"] = {top = "lfsr_m7_wrapper"},
    ["counter"] = {},
    ["sequence_detector"] = {},
    ["async_dual_ram"] = {},
    ["async_fifo"] = {},
    ["sync_dual_ram"] = {top = "sync_dual_ram_wrapper"},
}

for name, opt in pairs(rtl_verilator_target) do
    target(format("unit_test_rtl_%s_verilator", name))
        add_rules("verilator.shared")
        add_files(name..".sv")
        set_default(false)
        local top_module = opt["top"] or name
        add_values("verilator.flags", table.join(verilator_options, {"--top", top_module}))
        set_policy("build.fence", true)
        on_load(function (target)
            target:set("targetdir", path.join(target:targetdir(), name))
        end)
    target_end()
end
