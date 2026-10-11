set_policy("build.c++.modules", true)
add_rules("enable_lto")
-- 使单元测试安装后可以运行
add_rpathdirs("$ORIGIN/../lib", { installonly = true })

local function register_target(kind, enable)
    local suffix, rules, gen_json = get_target_settings(kind)
    local deps_suffix = suffix
    local coverage_mode = false
    -- 在覆盖率可用时启用覆盖率，但不改变目标名称
    if kind == "common" and enable_coverage() then
        coverage_mode = true
        deps_suffix, rules, _ = get_target_settings("coverage")
    end
    local prefix = kind == "common" and "" or (kind .. "_")
    local function common_settings()
        set_enabled(enable)
        set_default(false)
        add_rules(rules)
        set_policy("generator.compile_commands", gen_json)
    end

    target("unit_test_support" .. suffix, function ()
        set_kind("static")
        add_deps("verilator_utils_main" .. deps_suffix)
        add_files("common.cppm", { public = true })
        add_files("sanitizer_option.cpp")
        common_settings()

        after_load(function (target)
            -- 在未安装verilator的环境下首次config时避免访问空表
            local verilator_root = (target:pkgenvs() or {})["VERILATOR_ROOT"]
            if verilator_root then
                for _, runtime in ipairs({ "verilated.cpp", "verilated_threads.cpp", "verilated_cov.cpp" }) do
                    target:add("files", path.join(verilator_root, "include", runtime), { warnings = "none" })
                end
            end
        end)
    end)

    if kind == "fuzz" then return end

    target("unit_test" .. suffix, function ()
        set_group(prefix .. "unit_test")
        add_deps("unit_test_support" .. suffix)
        local regex = "*.cpp|rtl_*.cpp|sanitizer_option.cpp"
        add_files(regex)
        common_settings()

        for _, file in ipairs(os.files(regex)) do
            local name = path.basename(file)
            add_tests(name, {
                runargs = { "-ts=verilator_utils/" .. name, "-fc" },
                runenvs = {
                    LLVM_PROFILE_FILE = string.format("unit_test_%s.profraw", name)
                }
            })
        end

        after_clean(function (target)
            -- 删除覆盖率相关的文件
            if coverage_mode then
                os.rm(path.join(target:targetdir(), "*.prof*"), { async = true, detach = true })
                os.rm(path.join(target:targetdir(), "unit_test_coverage"), { async = true, detach = true })
            end
        end)
    end)

    for name, _ in pairs(rtl_verilator_target) do
        target("unit_test_rtl_" .. name .. suffix, function ()
            set_group(prefix .. "unit_test_rtl")
            add_deps(format("unit_test_rtl_%s_verilator", name), "verilator_utils_main" .. deps_suffix)
            if get_config("trace_support_fst") then
                add_packages("zlib", "lz4")
            end
            add_packages("libnpy-matajoh")
            add_files(format("rtl_%s*.cpp", name), "sanitizer_option.cpp")
            add_defines("VERILATOR_TRACER=" .. (get_config("trace_support_fst") and "VerilatedFstC" or "VerilatedVcdC"))
            add_tests("rtl", { runargs = { "+verilator+rand+reset+2", "-fc" } })
            common_settings()

            on_load(function (target)
                target:set("targetdir", path.join(target:targetdir(), name))
            end)

            -- 由该测试目标统一清理生成和测试过程中产生的文件
            after_clean(function (target)
                os.rm(target:targetdir(), { async = true, detach = true })
            end)
        end)
    end
end

register_target("common", get_config("enable_test"))
register_target("tysan", enable_tysan())
register_target("fuzz", enable_fuzz())
includes("fuzz/xmake.lua")
