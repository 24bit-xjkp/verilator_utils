set_policy("build.c++.modules", true)
target("verilator_utils")
    set_kind("static")
    add_files("*.cpp|main.cpp|factory.cpp", {public = true})
    add_includedirs(".", {public = true})
    add_headerfiles("*.hpp")
    if get_config("use_sanitizer") then
        set_policy("build.sanitizer.address", true)
        set_policy("build.sanitizer.undefined", true)
    end
target_end()

for _, tracer in ipairs({"vcd", "fst", "saif"}) do
    target("verilator_utils_" .. tracer)
        set_kind("static")
        -- libc++下，跨DSO使用RTTI会导致dynamic_cast失败，此处实现为在DSO内部创建对象再传出DSO
        add_cxflags("-fPIC")
        add_defines("USE_" .. string.upper(tracer))
        add_deps("verilator_utils")
        add_files("factory.cpp")
        local is_enabled = get_config("trace_support_" .. tracer)
        set_enabled(is_enabled)
        if tracer == "fst" and is_enabled then
            add_packages("zlib", "lz4", {public = true})
        end
    target_end()
end

target("verilator_utils_full")
    set_kind("static")
    add_deps("verilator_utils")
    add_files("main.cpp")
    set_policy("build.merge_archive", true)
target_end()
