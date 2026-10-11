local default_fuzz_args = {
    -- 单个输入的运行时间上限：调度器目标的部分输入需要较长时间，这里给足余量
    "-timeout=5",
    format("-max_total_time=%d", get_config("fuzz_time") or 120),
    format("-fork=%d", get_config("fuzz_parallel") or 2),
    "-use_value_profile=1"
}
local function common_settings()
    add_rules("enable_sanitizer", "fuzzer")
    set_enabled(enable_fuzz())
    set_default(false)
end

-- 模糊测试目标的公共模块：输入解码、不变量检查与断言配置
target("fuzz_test_support", function ()
    set_kind("object")
    add_deps("unit_test_support_fuzz")
    add_files("support.cppm", { public = true })
    common_settings()
end)

for _, file in ipairs(os.files("*.cpp")) do
    local name = path.basename(file)
    local target_name = "fuzz_test_" .. name
    target(target_name, function ()
        add_deps("fuzz_test_support")
        add_files(file)
        set_group("fuzz_test")
        common_settings()

        on_load(function (target)
            -- 测试进程的工作目录与项目根目录不同，因此语料目录必须使用绝对路径
            local target_dir = path.join(target:targetdir(), target_name)
            target:set("targetdir", target_dir)
            -- 种子语料与语料目录并列，libFuzzer只向作为第一个参数的语料目录写入新输入
            local fuzz_args = table.join(default_fuzz_args, "-artifact_prefix=artifacts/", "corpus", "seed")
            target:add("tests", "fuzz", { runargs = fuzz_args })
            target:set("runargs", fuzz_args)
        end)

        local function extract_corpus(target)
            local target_dir = target:targetdir()
            local seed_dir = path.join(target_dir, "seed")
            for _, dir in ipairs({ path.join(target_dir, "corpus"), seed_dir, path.join(target_dir, "artifacts") }) do
                os.mkdir(dir)
            end
            -- 精简语料库以tar.gz形式随仓库提供，种子目录为空时解压到种子目录
            if #os.files(path.join(seed_dir, "*")) == 0 then
                local archive = path.join(os.scriptdir(), "corpus", name .. ".tar.gz")
                if os.isfile(archive) then
                    import("utils.archive.extract")(archive, seed_dir)
                else
                    wprint("精简语料库%s不存在，本次运行不使用种子语料", archive)
                end
            end
        end

        before_test(extract_corpus)
        before_run(extract_corpus)

        -- 二进制、工作语料、种子语料与崩溃输入都在目标目录下，由该目标统一清理
        after_clean(function (target)
            os.rm(target:targetdir(), { async = true, detach = true })
        end)
    end)
end
