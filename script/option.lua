option("use_sanitizer", function ()
    set_default(false)
    set_description("Enable sanitizer for unit tests.")
end)

std_harden_defines = {
    "_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_" .. (is_mode("debug") and "DEBUG" or "FAST"), "_GLIBCXX_ASSERTIONS"
}
function get_std_harden_options()
    if not get_config("use_std_harden") then
        return nil
    end
    local options = {}
    for _, define in ipairs(std_harden_defines) do
        table.insert(options, "-D" .. define)
    end
    return options
end
option("use_std_harden", function ()
    set_default(false)
    set_description("Enable c++ standard library harden.")
    add_defines(std_harden_defines)
end)

option("use_lto", function ()
    set_default(false)
    set_description("Enable link time optimization.")
end)

option("trace_support_fst", function ()
    set_default(true)
    set_description("Enable FST trace support. This feature needs zlib and lz4.")
end)

option("check_kind", function ()
    set_values(false)
    set_showmenu(false)
    set_description([[Check the build kind. "static" and "shared" are supported.]])

    on_check(function (option)
        local kind = get_config("kind")
        assert(kind == "static" or kind == "shared", [[The kind "%s" is not supported.]], kind)
        option:enable(true)
    end)
end)

option("with_main", function ()
    set_default(true)
    set_description("Enable main function support.")
end)
