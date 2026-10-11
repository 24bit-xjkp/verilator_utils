--- 获取目标配置
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

--- 判断是否启用tysan系列目标
function enable_tysan()
    return get_config("enable_test") and get_config("enable tysan")
end

--- 判断是否启用fuzz系列目标
function enable_fuzz()
    return get_config("enable_fuzz")
end

--- 判断是否启用覆盖率支持
function enable_coverage()
    return is_mode("debug") and get_config("enable_test")
end
