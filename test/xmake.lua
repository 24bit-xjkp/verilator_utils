set_policy("build.c++.modules", true)
add_rules("enable_sanitizer", "enable_lto")
-- 使单元测试安装后可以运行
add_rpathdirs("$ORIGIN/../lib", { installonly = true })

target("unit_test", function ()
    set_enabled(get_config("enable_test"))
    set_group("unit_test")
    add_deps("verilator_utils_main")
    set_default(false)
    local regex = "*.cpp|rtl_*.cpp|common.cpp|sanitizer_option.cpp"
    add_files(regex, "common.cpp", "sanitizer_option.cpp")
    for _, file in ipairs(os.files(regex)) do
        local name = path.basename(file)
        add_tests(name, { runargs = { "-ts=verilator_utils/" .. name, "-fc" } })
    end
    after_load(function (target)
        -- 在未安装verilator的环境下首次config时避免访问空表
        local verilator_root = (target:pkgenvs() or {})["VERILATOR_ROOT"]
        if verilator_root then
            target:add("files", path.join(verilator_root, "include", "verilated.cpp"), { warnings = "none" })
            target:add("files", path.join(verilator_root, "include", "verilated_threads.cpp"), { warnings = "none" })
            -- dut_context的析构函数引用了覆盖率接口
            target:add("files", path.join(verilator_root, "include", "verilated_cov.cpp"), { warnings = "none" })
        end
    end)
end)

for name, _ in pairs(rtl_verilator_target) do
    target("unit_test_rtl_" .. name, function ()
        set_enabled(get_config("enable_test"))
        set_group("unit_test_rtl")
        add_deps(format("unit_test_rtl_%s_verilator", name), "verilator_utils_main")
        if get_config("trace_support_fst") then
            add_packages("zlib", "lz4")
        end
        add_packages("libnpy-matajoh")
        set_default(false)
        add_files(format("rtl_%s*.cpp", name), "sanitizer_option.cpp")
        add_defines("VERILATOR_TRACER=" .. (get_config("trace_support_fst") and "VerilatedFstC" or "VerilatedVcdC"))
        add_tests("rtl", { runargs = { "+verilator+rand+reset+2", "-fc" } })
        on_load(function (target)
            target:set("targetdir", path.join(target:targetdir(), name))
        end)

        -- 由该测试目标统一清理生成和测试过程中产生的文件
        after_clean(function (target)
            os.rm(target:targetdir(), { async = true, detach = true })
        end)
    end)
end
