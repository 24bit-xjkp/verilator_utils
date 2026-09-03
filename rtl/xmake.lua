local verilator_options = {
    "-Wall",
    get_config("trace_support_fst") and "--trace-fst" or "--trace-vcd",
    {"--x-assign", "unique"},
    {"--x-initial", "unique"},
    "--coverage",
    "-DSIMULATION=1",
    "-Irtl"
}
set_warnings("none")
add_toolchains("@verilator")

-- top: 设置顶层模块名
-- python: 设置python脚本，期待一个数组，元素格式为{file = "xxx.py", args(optional) = {...}}或"xxx.py"
rtl_verilator_target = {
    ["edge_detector"] = {},
    ["lfsr_m7"] = {top = "lfsr_m7_wrapper"},
    ["counter"] = {},
    ["sequence_detector"] = {},
    ["async_dual_ram"] = {},
    ["async_fifo"] = {},
    ["sync_dual_ram"] = {top = "sync_dual_ram_wrapper"},
    ["sync_fifo"] = {},
}

for name, opt in pairs(rtl_verilator_target) do
    local python_target_name
    if opt.python then
        python_target_name = format("unit_test_rtl_%s_python", name)
        target(python_target_name)
            set_kind("object")
            set_enabled(get_config("enable_test"))
            add_rules("python")
            set_default(false)
            set_policy("build.fence", true)
            on_load(function (target)
                assert(table.is_array(opt.python), "python应当是一个数组")
                local additional_args = {"-o", path.join(target:targetdir(), name)}
                local files = {}
                local python_args = {}
                for _, python in ipairs(opt.python) do
                    local file
                    local args
                    if type(python) == "string" then
                        file = python
                        args = {}
                    elseif type(python) == "table" and table.is_dictionary(python) then
                        file = python.file
                        args = python.args or {}
                    else
                        raise("不支持的元素类型，python数组的元素应当为字符串或字典")
                    end
                    files = table.append(files, path.join(target:scriptdir(), file))
                    args = table.join(additional_args, args)
                    python_args = table.append(python_args, {file = file, args = args})
                end
                target:add("files", files)
                target:add("values", "python.args", python_args)
            end)
        target_end()
    end

    target(format("unit_test_rtl_%s_verilator", name))
        set_enabled(get_config("enable_test"))
        add_rules("verilator.shared")
        add_files(name..".sv")
        set_default(false)
        local top_module = opt.top or name
        add_values("verilator.flags", table.join(verilator_options, {"--top", top_module}))
        set_policy("build.fence", true)
        if opt.python then
            add_deps(python_target_name)
        end
        on_load(function (target)
            target:set("targetdir", path.join(target:targetdir(), name))
        end)
    target_end()
end
