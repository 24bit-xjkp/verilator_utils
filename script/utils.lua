--- 获取目标配置
---@param kind string
function get_target_settings(kind)
    local suffix
    local sanitizer_rule
    local generate_compile_commands_json

    if kind == "common" then
        suffix = ""
        sanitizer_rule = "enable_sanitizer"
        generate_compile_commands_json = true
    elseif kind == "tysan" then
        suffix = "_tysan"
        sanitizer_rule = "enable_type_sanitizer"
        generate_compile_commands_json = false
    end

    return suffix, sanitizer_rule, generate_compile_commands_json
end

--- 判断是否启用tysan系列目标
function enable_tysan()
    return get_config("enable_test") and get_config("enable tysan")
end
