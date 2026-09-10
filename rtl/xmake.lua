local gen_src_dir = path.join(os.projectdir(), get_config("builddir"), ".gens", "system_verilog")
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

-- name: 目标名称，作为rtl_verilator_target表的键
-- 可能的值:
-- top: 设置顶层模块名
-- python: 设置python脚本，期待一个数组或字符串，文件路径相对于target:scriptdir()
-- 数组元素格式为{file = "xxx.py", args(optional) = {...}}或"xxx.py"
-- 在只有一个脚本且不传递额外参数时，可以简写成字符串形式，即"xxx.py"
-- gen_src: 设置由脚本生成的源文件列表，期待一个数组或字符串，文件路径相对于gen_src_dir/name
-- 数组元素为文件路径，若只有一个可简写成字符串形式
rtl_verilator_target = {
    edge_detector = {},
    lfsr_m7 = {top = "lfsr_m7_wrapper"},
    counter = {},
    sequence_detector = {},
    async_dual_ram = {},
    async_fifo = {},
    sync_dual_ram = {top = "sync_dual_ram_wrapper"},
    sync_fifo = {},
    cic_filter = {python = "cic_filter.py", gen_src = "cic_filter_param.sv"},
}

for name, opt in pairs(rtl_verilator_target) do
    local python_target_name
    local target_gen_src_dir = path.join(gen_src_dir, name)
    if opt.python then
        python_target_name = format("unit_test_rtl_%s_python", name)
        target(python_target_name)
            set_kind("object")
            set_enabled(get_config("enable_test"))
            add_rules("python")
            set_default(false)
            set_policy("build.fence", true)
            on_load(function (target)
                local additional_args = {
                    "-o", path.join(target:targetdir(), name),
                    "-g", target_gen_src_dir,
                    get_config("visualize") and "--visualize" or "--no-visualize"
                }
                local prefix = path.relative(target:scriptdir(), os.projectdir())
                for _, python in ipairs(table.wrap(opt.python)) do
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
                    local file_path = path.join(prefix, file)
                    target:add("files", file_path)
                    target:add("values", "python.args." .. file_path, table.join(additional_args, args))
                end

                -- 创建文件，避免add_files失败
                for _, file in ipairs(table.wrap(opt.gen_src or {})) do
                    os.touch(path.join(target_gen_src_dir, file))
                end
            end)

            after_clean(function (_)
                -- 删除生成的源代码，生成的数据由最终的rtl测试目标进行删除
                os.rm(target_gen_src_dir)
            end)
        target_end()
    end

    target(format("unit_test_rtl_%s_verilator", name))
        set_enabled(get_config("enable_test"))
        add_rules("verilator.shared")
        add_files(name..".sv")
        for _, file in ipairs(table.wrap(opt.gen_src or {})) do
            add_files(path.join(target_gen_src_dir, file))
        end
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
