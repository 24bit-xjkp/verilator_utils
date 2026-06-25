target("unit_test_main")
    set_default(false)
    set_kind("object")
    add_files("main.cpp")
target_end()

if get_config("use_sanitizer") then
    set_policy("build.sanitizer.address", true)
    set_policy("build.sanitizer.undefined", true)
end

target("unit_test")
    add_deps("verilator_utils", "unit_test_main")
    set_policy("build.c++.modules", true)
    set_default(false)
    local regex = "*.cpp|rtl_*.cpp|main.cpp"
    add_files(regex)
    for _, file in ipairs(os.files(regex)) do
        local name = path.basename(file)
        add_tests(name, {runargs = {"-ts=verilator_utils/" .. name}})
    end
    after_load(function (target)
        local verilator_root = target:pkgenvs()["VERILATOR_ROOT"];
        target:add("files", path.join(verilator_root, "include", "verilated.cpp"))
        target:add("files", path.join(verilator_root, "include", "verilated_threads.cpp"))
    end)
target_end()

target("unit_test_rtl_edge_detector")
    add_deps("unit_test_rtl_edge_detector_verilator", "verilator_utils_full")
    set_default(false)
    set_policy("build.c++.modules", true)
    add_files("rtl_edge_detector.cpp")
    add_tests("rtl")
target_end()
