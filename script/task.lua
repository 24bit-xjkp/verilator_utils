task("verible-file-list", function ()
    set_category("plugin")
    set_menu {
        usage = "xmake verible-file-list [options]",
        description = "为 verible 生成 RTL 文件列表",
        options = {
            { nil, "output_dir", "v", ".vscode", "设置输出目录" }
        }
    }

    on_run(function ()
        import("core.base.option")
        import("core.project.config")
        local project_dir = os.projectdir()
        local output_dir = path.join(project_dir, option.get("output_dir"))
        if not os.exists(output_dir) then
            os.mkdir(output_dir)
        end

        local file_list = {}
        for _, filepath in ipairs(os.files(path.join(project_dir, "rtl", "*.sv"))) do
            table.insert(file_list, filepath)
        end

        local build_dir = config.builddir()
        local gen_file_list = {}
        for _, filepath in ipairs(os.files(path.join(build_dir, ".gens", "system_verilog", "**.sv"))) do
            table.insert(file_list, filepath)
            table.insert(gen_file_list, filepath)
        end

        local output_file = path.join(output_dir, "verible.filelist")
        io.writefile(output_file, table.concat(file_list, "\n"))
        local output_file = path.join(output_dir, "verible_gen.filelist")
        io.writefile(output_file, table.concat(gen_file_list, "\n"))
        cprint("${color.success}生成 verible.filelist 成功")
    end)
end)

task("llvm-profdata", function ()
    on_run(function ()
        import("core.base.option")
        import("core.project.project")
        import("core.project.config")
        import("lib.detect.find_program")
        config.load()

        local target_name = option.get("target")
        local target = project.target(target_name)
        local target_dir = target:targetdir()
        -- 查找 llvm-profdata 工具
        local llvm_profdata = find_program("llvm-profdata")
        assert(llvm_profdata, "未找到 llvm-profdata")
        -- 获取输出文件名称，默认是 <target>.profdata
        local profdata = option.get("profdata") or (target_name .. ".profdata")
        -- 进入目标所在目录
        os.cd(target_dir)
        -- 查找所有的 profraw 文件
        local files = os.files(target_name .. "*.profraw")
        assert(#files ~= 0, [[目标 "%s" 没有 profraw 文件]], target_name)
        local args = { "merge" }
        if option.get("sparse") then
            table.insert(args, "-sparse")
        end
        args = table.join(args, files, { "-o", profdata })
        -- 合并所有的 profraw 文件到 profdata 文件
        os.vrunv(llvm_profdata, args)
    end)
    set_category("plugin")
    set_menu {
        usage = "xmake llvm-profdata [option]",
        description = "使用 llvm-profdata 将 profraw 文件合并为 profdata 文件",
        options = {
            { nil, "sparse", "kv", true, "启用稀疏 profraw 文件" },
            { nil, "profdata", "kv", nil, "设置输出的 profdata 文件名，默认为 <target>.profdata" },
            { nil, "target", "v", nil, "设置目标" }
        }
    }
end)

task("llvm-cov", function ()
    on_run(function ()
        import("core.base.option")
        import("core.project.project")
        import("core.project.config")
        import("lib.detect.find_program")
        config.load()

        -- 查找 llvm-cov 工具
        local llvm_cov = find_program("llvm-cov")
        assert(llvm_cov, "未找到 llvm-cov")
        local task = option.get("task")
        assert(
            table.contains({ "export", "report", "show" }, task),
            [[任务必须是 "export"、"report"、"show" 之一]]
        )
        local default_format
        if task == "show" then
            default_format = "html"
        elseif task == "export" then
            default_format = "lcov"
        elseif task == "report" then
            default_format = "text"
        end
        local format = option.get("format") or default_format
        local target_names = option.get("targets")
        ---@type unknown[]
        local targets = {}
        for _, target_name in ipairs(target_names) do
            table.insert(targets, project.target(target_name))
        end
        -- 获取主目标
        local main_target = targets[1]
        local main_target_name = main_target:name()
        local main_target_file = main_target:filename()
        -- 获取主目标所在目录作为新的工作目录
        local main_target_dir = main_target:targetdir()
        -- 设置输出目录，默认是 <target>_coverage 目录
        local output_dir = main_target_name .. "_coverage"
        local output_option = option.get("output")
        local default_output_option = task == "show" and output_dir or path.join(output_dir, "lcov.info")
        local output_path = output_option and path.relative(output_option, main_target_dir) or default_output_option
        -- 获取profdata文件名称，默认是 <target>.profdata
        local profdata_option = option.get("profdata")
        local default_profdata = main_target_name .. ".profdata"
        local profdata = profdata_option and path.relative(profdata_option, main_target_dir) or default_profdata
        local actual_profdata_path = path.join(main_target_dir, profdata)
        assert(os.isfile(actual_profdata_path), [[Profdata file "%s" not found!]], actual_profdata_path)
        local args = { task, main_target_file, "-instr-profile=" .. profdata, "-format=" .. format }
        -- 添加其他目标文件到参数中
        local extra_targets = table.slice(targets, 2)
        for _, target in ipairs(extra_targets) do
            table.join2(args, { "-object", path.relative(target:targetfile(), main_target_dir) })
        end
        -- 启用 MC/DC 覆盖率
        local mcdc_flag_table = { show = "-show-mcdc", report = "-show-mcdc-summary", export = nil }
        local mcdc_flag = mcdc_flag_table[task]
        if option.get("mcdc") and mcdc_flag then
            table.insert(args, mcdc_flag)
        end
        if task == "show" then
            -- 设置输出路径
            table.insert(args, "-output-dir=" .. output_path)
            -- 启用分支覆盖率
            if option.get("branch") then
                table.insert(args, "--show-branches=count")
            end
            -- 启用宏展开覆盖率
            if option.get("expansion") then
                table.insert(args, "--show-expansions")
            end
        end
        -- 进入目标所在目录
        os.cd(main_target_dir)
        -- 执行 llvm-cov 工具
        if task ~= "export" then
            os.execv(llvm_cov, args)
        else
            if not os.isdir(output_dir) then
                os.mkdir(output_dir)
            end
            local outdata, _ = os.iorunv(llvm_cov, args)
            io.writefile(output_path, outdata)
        end
    end)
    set_category("plugin")
    set_menu {
        usage = "xmake llvm-cov [option]",
        description = "使用 llvm-cov 显示代码覆盖率",
        options = {
            {
                "t",
                "task",
                "v",
                nil,
                "设置任务名称",
                "    - export",
                "    - report",
                "    - show"
            },
            {
                nil,
                "profdata",
                "kv",
                nil,
                [[设置输入的 profdata 文件名，默认为 <target>.profdata；若设置了 "exec"，则为 <exec>.profdata]]
            },
            { nil, "mcdc", "kv", true, "启用 MC/DC 覆盖率" },
            { nil, "branch", "kv", true, "启用分支覆盖率" },
            { nil, "expansion", "kv", true, "启用宏展开覆盖率" },
            {
                "f",
                "format",
                "kv",
                nil,
                [[设置输出格式："show" 任务默认为 "html"，"export" 任务默认为 "lcov"，"report" 任务默认为 "text"]],
                "    - html",
                "    - lcov",
                "    - text"
            },
            {
                "o",
                "output",
                "kv",
                nil,
                [[设置输出路径："show" 任务默认为 <target>_coverage 目录，"export" 任务默认为 <target>_coverage/lcov.info]]
            },
            { nil, "targets", "vs", nil, "设置目标，第一个目标是用于查找 profdata 的主目标" }
        }
    }
end)

task("update-corpus", function ()
    set_category("plugin")
    set_menu {
        usage = "xmake update-corpus [option]",
        description = "更新模糊测试语料库",
        options = {
            {
                "o",
                "output",
                "kv",
                nil,
                "设置输出路径，默认为test/fuzz/corpus"
            },
            { "j", "jobs", "kv", os.default_njob(), "并行任务数" },
            { nil, "targets", "vs", nil, "设置目标，默认为fuzz_test组的全部目标" }
        }
    }

    on_run(function ()
        import("async.jobgraph")
        import("async.runjobs")
        import("core.base.option")
        import("core.project.project")
        import("core.project.config")
        import("core.base.task")
        import("utils.archive")
        import("utils.progress")
        config.load()

        -- 获取所有模糊测试目标
        local fuzz_test_targets = {}
        for name, target in pairs(project.targets()) do
            if target:get("group") == "fuzz_test" then
                fuzz_test_targets[name] = target
            end
        end
        -- 获取要处理的目标
        ---@type any
        local targets_to_process = {}
        local targets = option.get("targets")
        if targets == nil then
            targets_to_process = table.values(fuzz_test_targets)
        else
            for _, target_name in ipairs(targets) do
                ---@type table?
                local target = fuzz_test_targets[target_name]
                assert(target, [[目标"%s"不在fuzz_test组中]], target_name)
                table.append(targets_to_process, target)
            end
        end
        -- 获取输出目录
        local default_output_dir = path.join(os.scriptdir(), "..", "test", "fuzz", "corpus")
        local output_dir = option.get("output") or default_output_dir
        os.mkdir(output_dir)
        -- 创建任务图
        local jobs = jobgraph.new()
        jobs:group("fuzz/update-corpus", function ()
            for _, target in ipairs(targets_to_process) do
                local name = target:name()
                local job_name = name .. "/corpus"
                local target_dir = target:targetdir()
                -- libFuzzer只在输出目录为空时精简语料，写入非空的corpus目录只会追加新条目，
                -- 因此精简结果先落到空的暂存目录，工作语料保持不变
                local merged_dir = path.join(target_dir, "merged")
                -- 精简时沿用模糊测试的参数，保留value profile特征
                local args = {
                    "run", name, "-merge=1", "-use_value_profile=1", "merged", "corpus", "seed", "artifacts"
                }
                -- 去除fuzz_test_前缀
                local pure_name = string.sub(name, 11)
                local archive_path = path.absolute(path.join(output_dir, pure_name .. ".tar.gz"))
                jobs:add(job_name, function (_, _, opt)
                    progress.show(opt.progress, "${color.build.object}merging.corpus %s", name)
                    os.tryrm(merged_dir)
                    os.mkdir(merged_dir)
                    os.vrunv("xmake", args)
                    progress.show(opt.progress, "${color.build.target}archiving.corpus %s", name)
                    archive.archive(archive_path, ".", { curdir = merged_dir, compress = "best" })
                end)
            end
        end)
        runjobs("update-corpus", jobs, { comax = option.get("jobs") })
    end)
end)
