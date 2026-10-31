---获取目标配置
---@param kind string
function get_target_settings(kind)
    local suffix
    ---@type any
    local rules
    local generate_compile_commands_json

    if kind == "common" then
        suffix = ""
        rules = "enable_sanitizer"
        generate_compile_commands_json = true
    elseif kind == "tysan" then
        suffix = "_tysan"
        rules = "enable_type_sanitizer"
        generate_compile_commands_json = false
    elseif kind == "coverage" then
        suffix = "_coverage"
        rules = { "enable_sanitizer", "coverage" }
        generate_compile_commands_json = false
    elseif kind == "fuzz" then
        suffix = "_fuzz"
        rules = { "enable_sanitizer", "fuzzer" }
        generate_compile_commands_json = false
    end

    return suffix, rules, generate_compile_commands_json
end

---判断是否启用tysan系列目标
function enable_tysan()
    return get_config("enable_test") and get_config("enable tysan")
end

---判断是否启用fuzz系列目标
function enable_fuzz()
    return get_config("enable fuzz")
end

---判断是否启用覆盖率支持
function enable_coverage()
    return is_mode("debug") and get_config("enable_test")
end

---检查targets是否属于group组
---@param targets string[]
---@param group string
---@return table[] target对象数组
function check_target_in_group(targets, group)
    import("core.project.config").load()

    -- 获取组内目标
    local targets_in_group = {}
    for name, target in pairs(import("core.project.project").targets()) do
        if target:get("group") == group then
            targets_in_group[name] = target
        end
    end
    -- 获取要处理的目标
    if targets == nil then
        return table.values(targets_in_group)
    end
    local targets_to_process = {}
    for _, target_name in ipairs(targets) do
        ---@type table?
        local target = targets_in_group[target_name]
        assert(target, [[目标"%s"不在%s组中]], target_name, group)
        table.append(targets_to_process, target)
    end
    return targets_to_process
end
