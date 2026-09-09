set_project("verilator_utils")
-- 3.1.0修复了Verilator与C++模块共同使用时的编译顺序问题
set_xmakever("3.1.0")
set_version("0.1.0")
set_languages("c++latest")
set_warnings("allextra")
includes("script/*.lua")
add_rules("mode.debug", "mode.release", "mode.releasedbg")
set_allowedmodes("debug", "release", "releasedbg")
-- Verilator v5.050修复了clang编译问题
add_requires("verilator >=5.050")
add_packages("verilator")
local config = {
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
add_rules("verilator_include")
if get_config("trace_support_fst") then
    add_requires("zlib", "lz4")
end
if get_config("enable_test") then
    add_requires("libnpy-matajoh", config)
    add_requireconfs("libnpy-matajoh", { configs = { cxxflags = get_std_harden_options() } })
end

includes("*/xmake.lua")
