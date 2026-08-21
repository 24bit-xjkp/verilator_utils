set_project("verilator_utils")
set_languages("c++latest")
set_warnings("allextra")
includes("script/*.lua")
add_rules("mode.debug", "mode.release", "mode.releasedbg")
set_allowedmodes("debug", "release", "releasedbg")
add_requires("verilator")
add_packages("verilator")
local config = {
    debug = is_mode("debug"),
    configs = {
        shared = is_kind("shared"),
        asan = get_config("use_sanitizer"),
        lto = get_config("use_lto")
    }
}
add_requires("doctest_module", "cpptrace", config)
add_requireconfs("doctest_module", { configs = { main = false, std_harden = get_config("use_std_harden") } })
add_requireconfs("cpptrace", { configs = { cxxflags = get_std_harden_options() } })
set_exceptions("cxx")
set_policy("build.c++.modules.hide_dependencies", true)
set_defaultmode("release")

add_options("use_std_harden")
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
