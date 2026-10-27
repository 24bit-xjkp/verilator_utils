set_project("verilator_utils")
-- 3.1.1引入addon支持
set_xmakever("3.1.1")
set_version("0.1.0")
set_languages("c++latest")
set_warnings("allextra", "pedantic")
includes("script/*.lua")
add_rules("mode.debug", "mode.release", "mode.releasedbg")
set_allowedmodes("debug", "release", "releasedbg")
add_addons("format-plugin", "doxygen-plugin")
-- Verilator v5.050修复了clang编译问题
add_requires("doctest_module", "cpptrace", "verilator >=5.050")
add_packages("verilator")
-- 未插桩的代码和插桩代码需要通过动态链接隔离
add_requireconfs("cpptrace", { configs = { shared = get_config("use_sanitizer") } })
set_exceptions("cxx")
set_policy("build.c++.modules.hide_dependencies", true)
set_defaultmode("release")

add_options("use_std_harden")
add_rules("verilator_include")
if get_config("trace_support_fst") then
    add_requires("zlib", "lz4")
end
if get_config("enable_test") then
    add_requires("libnpy-matajoh")
end

includes("*/xmake.lua")
