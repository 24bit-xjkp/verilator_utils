option("use_sanitizer", function ()
    set_default(false)
    set_description("启用清洁器支持")
end)

option("type sanitizer support", function ()
    set_showmenu(false)
    set_description("工具链是否支持TySan (-fsanitize=type)")

    on_check(function (option)
        option:enable(import("core.tool.compiler").has_flags("cxx", "-fsanitize=type"))
    end)
end)

option("use_type_sanitizer", function ()
    set_default(false)
    set_description("在可能时为单元测试启用TySan支持")
end)

option("enable tysan", function ()
    set_showmenu(false)
    add_deps("type sanitizer support")
    set_description("在单元测试中启用TySan支持")

    on_check(function (option)
        if get_config("use_type_sanitizer") then
            option:enable(true)
            if not get_config("type sanitizer support") then
                cprint([[${color.warning}当前工具链不支持TySan，忽略"use_type_sanitizer"选项]])
                option:enable(false)
            end
            if not get_config("use_sanitizer") then
                cprint([[${color.warning}未启用"use_sanitizer"选项，忽略"use_type_sanitizer"选项]])
                option:enable(false)
            end
        end
    end)
end)

std_harden_defines = {
    "_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_" .. (is_mode("debug") and "DEBUG" or "FAST"), "_GLIBCXX_ASSERTIONS"
}
function get_std_harden_options()
    if not get_config("use_std_harden") then
        return nil
    end
    local options = {}
    for _, define in ipairs(std_harden_defines) do
        table.insert(options, "-D" .. define)
    end
    return options
end
option("use_std_harden", function ()
    set_default(true)
    set_description("启用C++标准库硬化")
    add_defines(std_harden_defines)
end)

option("use_lto", function ()
    set_default(false)
    set_description("启用链接时优化")
end)

option("trace_support_fst", function ()
    set_default(true)
    set_description("启用FST波形支持，依赖zlib和zstd")
end)

option("target kind", function ()
    set_values(false)
    set_showmenu(false)
    set_description([[检查目标类型，只支持static和shared]])

    on_check(function (option)
        local kind = get_config("kind")
        assert(kind == "static" or kind == "shared", [[不支持的目标类型: %s]], kind)
        option:enable(true)
    end)
end)

option("with_main", function ()
    set_default(true)
    set_description("开启main函数支持")
end)

option("64bit platform", function ()
    set_showmenu(false)
    set_description("判断目标平台是否为64位平台")

    add_cxxsnippets("sizeof(void*)", "static_assert(sizeof(void*) == 8);")
    after_check(function (option)
        assert(option:enabled(), "只支持64位平台")
    end)
end)

option("enable_test", function ()
    set_default(true)
    set_description("启用测试目标，是测试的总开关")
end)

option("visualize", function ()
    set_default(true)
    set_description("启用Python脚本中的可视化支持")
end)

option("asan uas support", function ()
    set_showmenu(false)
    set_description("启用ASan的Use-After-Scope检查")

    on_check(function (option)
        option:enable(import("core.tool.compiler").has_flags("cxx", "-fsanitize-address-use-after-scope"))
    end)
end)

option("enable_fuzz", function ()
    set_default(true)
    set_description("启用模糊测试目标")
end)

option("libFuzzer support", function ()
    set_showmenu(false)
    set_description("工具链是否支持libFuzze")

    on_check(function (option)
        option:enable(import("core.tool.compiler").has_flags("cxx", "-fsanitize=fuzzer-no-link"))
    end)
end)

option("enable fuzz", function ()
    set_showmenu(false)
    add_deps("libFuzzer support")
    set_description("启用模糊测试支持")

    on_check(function (option)
        if get_config("enable_fuzz") then
            option:enable(true)
            if not get_config("libFuzzer support") then
                cprint([[${color.warning}当前工具链不支持libFuzzer，忽略"enable_fuzz"选项]])
                option:enable(false)
            end
            if not get_config("enable_test") then
                cprint([[${color.warning}未启用"enable_test"选项，忽略"enable_fuzz"选项]])
                option:enable(false)
            end
        end
    end)
end)

option("fuzz_time", function ()
    set_default(120)
    set_description("每个模糊测试目标的最大执行时间")
end)

option("fuzz_parallel", function ()
    set_default(2)
    set_description("每个模糊测试目标的并发数")
end)
