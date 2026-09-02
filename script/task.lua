task("verible_file_list", function ()
    set_category("plugin")
    set_menu {
        usage = "xmake verible_file_list [options]",
        description = "Generate RTL file list for verible.",
        options = {
            { nil, "output_dir", "v", ".vscode", "Set the output directory." }
        }
    }

    on_run(function ()
        import("core.base.option")
        local project_dir = os.projectdir()
        local output_dir = path.join(project_dir, option.get("output_dir"))
        if not os.exists(output_dir) then
            os.mkdir(output_dir)
        end

        local file_list = {}
        for _, filepath in ipairs(os.files(path.join(project_dir, "rtl", "*.sv"))) do
            table.insert(file_list, path.relative(filepath, project_dir))
        end

        local output_file = path.join(output_dir, "verible.filelist")
        io.writefile(output_file, table.concat(file_list, "\n"))
        cprint("${color.success}generate verible.filelist ok!")
    end)
end)
