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
