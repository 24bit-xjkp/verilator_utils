set_policy("build.c++.modules", true)
add_rules("enable_sanitizer", "enable_lto")

local sanitizer_envs = { "ASAN_OPTIONS=check_initialization_order=1" }

target("unit_test")
    set_enabled(get_config("enable_test"))
    set_group("unit_test")
    add_deps("verilator_utils_main")
    set_default(false)
    local regex = "*.cpp|rtl_*.cpp|common.cpp"
    add_files(regex, "common.cpp")
    for _, file in ipairs(os.files(regex)) do
        local name = path.basename(file)
        add_tests(name, { runargs = { "-ts=verilator_utils/" .. name, "-fc" }, runenvs = sanitizer_envs })
    end
    after_load(function (target)
        -- 在未安装verilator的环境下首次config时避免访问空表
        local verilator_root = (target:pkgenvs() or {})["VERILATOR_ROOT"]
        if verilator_root then
            target:add("files", path.join(verilator_root, "include", "verilated.cpp"), {warnings = "none"})
            target:add("files", path.join(verilator_root, "include", "verilated_threads.cpp"), {warnings = "none"})
        end
    end)
target_end()

for name, _ in pairs(rtl_verilator_target) do
    target("unit_test_rtl_"..name)
        set_enabled(get_config("enable_test"))
        set_group("unit_test_rtl")
        add_deps(format("unit_test_rtl_%s_verilator", name), "verilator_utils_main")
        if get_config("trace_support_fst") then
            add_packages("zlib", "lz4")
        end
        add_packages("cnpy")
        set_default(false)
        add_files(format("rtl_%s*.cpp", name))
        add_defines("VERILATOR_TRACER=" .. (get_config("trace_support_fst") and "VerilatedFstC" or "VerilatedVcdC"))
        add_tests("rtl", {runargs = {"+verilator+rand+reset+2", "-fc"}, runenvs = sanitizer_envs})
        on_load(function (target)
            target:set("targetdir", path.join(target:targetdir(), name))
        end)
    target_end()
end
