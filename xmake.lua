set_project("verilator_utils")
set_languages("c++latest")
set_warnings("allextra")
includes("script/*.lua")
add_rules("mode.debug", "mode.release", "mode.releasedbg")
add_requires("doctest", "verilator", "clean_std_heads")
add_packages("doctest", "verilator", "clean_std_heads")
set_exceptions("cxx")
add_cxxflags("-Wno-deprecated-missing-comma-variadic-parameter", "-Wno-sign-compare", "-Wno-unused-parameter")
set_policy("build.c++.modules.hide_dependencies", true)
set_defaultmode("release")

option("use_sanitizer")
    set_default(false)
    set_description("Enable sanitizer for unit tests")
option_end()

option("use_lto")
    set_default(false)
    set_description("Enable link-time optimization when building")
option_end()
if get_config("use_lto") then
    set_policy("build.optimization.lto", true)
end

option("trace_support_vcd")
    set_default(true)
    set_description("Enable VCD trace support.")
option_end()
option("trace_support_fst")
    set_default(true)
    set_description("Enable FST trace support.")
option_end()
option("trace_support_saif")
    set_default(true)
    set_description("Enable SAIF trace support.")
option_end()
if get_config("trace_support_fst") then
    add_requires("zlib", "lz4")
end

rule("verilator_include")
    after_load(function (target)
        target:add("includedirs", path.join(target:pkgenvs()["VERILATOR_ROOT"], "include"), {public = true})
    end)
rule_end()
add_rules("verilator_include")

includes("*/xmake.lua")
