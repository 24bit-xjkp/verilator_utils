set_policy("build.c++.modules", true)
target("unit_test_main")
    set_default(false)
    set_kind("static")
    add_files("main.cpp")
target_end()

if get_config("use_sanitizer") then
    set_policy("build.sanitizer.address", true)
    set_policy("build.sanitizer.undefined", true)
end

target("unit_test")
    add_deps("verilator_utils", "unit_test_main")
    set_default(false)
    local regex = "*.cpp|rtl_*.cpp|main.cpp"
    add_files(regex)
    for _, file in ipairs(os.files(regex)) do
        local name = path.basename(file)
        add_tests(name, {runargs = {"-ts=verilator_utils/" .. name}})
    end
    after_load(function (target)
        local verilator_root = target:pkgenvs()["VERILATOR_ROOT"];
        target:add("files", path.join(verilator_root, "include", "verilated.cpp"), {warnings = "none"})
        target:add("files", path.join(verilator_root, "include", "verilated_threads.cpp"), {warnings = "none"})
    end)
target_end()

for name, _ in pairs(rtl_verilator_target) do
    target("unit_test_rtl_"..name)
        add_deps(format("unit_test_rtl_%s_verilator", name), "verilator_utils_full")
        set_default(false)
        add_files(format("rtl_%s*.cpp", name))
        add_tests("rtl", {runargs = {"+verilator+rand+reset+2"}})
        on_load(function (target)
            target:set("targetdir", path.join(target:targetdir(), name))
        end)
    target_end()
end
