rule("verilator_include", function ()
    after_load(function (target)
        target:add("includedirs", path.join(target:pkgenvs()["VERILATOR_ROOT"], "include"), { public = true })
    end)
end)

rule("enable_sanitizer", function ()
    on_load(function (target)
        if get_config("use_sanitizer") then
            target:set("policy", "build.sanitizer.address", true)
            target:set("policy", "build.sanitizer.undefined", true)
        end
    end)
end)

rule("enable_lto", function ()
    on_load(function (target)
        if get_config("use_lto") then
            target:set("policy", "build.optimization.lto", true)
        end
    end)
end)

---@class python_arg_t
---@field public file string
---@field public args string[]?
---@alias python_args_t python_arg_t[]
rule("python", function ()
    set_extensions(".py")

    before_prepare_files(function (target, jobgraph, sourcebatch, _)
        import("lib.detect.find_tool")
        import("utils.progress")
        import("core.project.depend")

        local python = assert(find_tool("python"), "python not found!").program
        ---@type python_args_t
        local python_args = target:values("python.args") or {}
        ---@type table<string, string[]>
        local python_args_map = {}
        ---@diagnostic disable-next-line
        for _, python_arg in ipairs(python_args) do
            assert(type(python_arg) == "table" and table.is_dictionary(python_arg), "python.args的元素必须是表")
            local file_path = python_arg.file
            file_path = path.is_absolute(file_path) and file_path or path.join(target:scriptdir(), file_path)
            file_path = path.relative(path.normalize(file_path), os.projectdir())
            python_args_map[file_path] = python_arg.args or {}
        end
        local autogen_dir = path.join(target:autogendir(), "rules", "python")
        local scan_deps = path.join("script", "scan_deps.py")
        local group_name = target:name() .. "/python"

        jobgraph:group(group_name, function ()
            for _, sourcefile in ipairs(sourcebatch.sourcefiles) do
                local job_name = target:name() .. "/" .. sourcefile
                jobgraph:add(job_name, function (_, _, jobopt)
                    local args = python_args_map[sourcefile] or {}
                    local depvalues = { python, args }
                    local dependfile = path.join(autogen_dir, sourcefile .. ".d")
                    local scan_deps_file = path.join(autogen_dir, sourcefile .. "_scan_deps.d")
                    local scan_deps_args = { scan_deps, "-o", scan_deps_file, sourcefile }
                    local dependinfo = depend.load(dependfile) or {}
                    local sourcefiles = dependinfo.files or table.wrap(sourcefile)
                    local is_changed_opt = { lastmtime = os.mtime(dependfile), files = sourcefiles, values = depvalues }
                    if target:is_rebuilt() or depend.is_changed(dependinfo, is_changed_opt) then
                        progress.set_target(jobopt.progress, target)
                        progress.show(jobopt.progress, "${clear}generating.python.deps %s", sourcefile)
                        os.vrunv(python, scan_deps_args)
                        sourcefiles = string.split(io.readfile(scan_deps_file), "\n")
                        progress.show(jobopt.progress, "${color.build.object}executing.python %s", sourcefile)
                        os.vrunv(python, table.join(sourcefile, args))
                        depend.save({ files = sourcefiles, values = depvalues }, dependfile)
                    end
                end)
            end
        end)
    end, { jobgraph = true })
end)
