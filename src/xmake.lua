set_policy("build.c++.modules", true)
if get_config("use_sanitizer") then
    set_policy("build.sanitizer.address", true)
    set_policy("build.sanitizer.undefined", true)
end
if get_config("use_lto") then
    set_policy("build.optimization.lto", true)
end
target("verilator_utils")
    set_kind("$(kind)")
    add_packages("doctest_module", {components = "core", public = true})
    add_packages("cpptrace", {public = true})
    add_files("*.cppm", {public = true, install = true})
    add_includedirs(".", {public = true})
    add_headerfiles("*.hpp")
    set_group("verilator_utils")
target_end()

target("verilator_utils_main")
    set_kind("static")
    add_deps("verilator_utils")
    add_files("main.cpp")
    set_group("verilator_utils")
    set_default(get_config("with_main"))
target_end()
