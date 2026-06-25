target("verilator_utils")
    set_kind("moduleonly")
    add_files("*.cpp|main.cpp", {public = true})
    add_headerfiles("*.hpp")
    set_policy("build.c++.modules", true)
    if get_config("use_sanitizer") then
        set_policy("build.sanitizer.address", true)
        set_policy("build.sanitizer.undefined", true)
    end
target_end()

target("verilator_utils_full")
    set_kind("static")
    add_deps("verilator_utils")
    add_files("main.cpp")
    set_policy("build.merge_archive", true)
target_end()
