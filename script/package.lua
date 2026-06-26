package("clean_std_heads")
    set_homepage("https://github.com/YexuanXiao/convert-cpp-std-headers-to-std-module")
    set_urls("https://github.com/YexuanXiao/convert-cpp-std-headers-to-std-module/archive/refs/tags/$(version).zip")
    add_versions("2026-06-26", "829ecc03ccb0dca7cdde504ddb931f76669d47d190443b16dfb8e8f5fb1da56a")
    set_kind("library", {headeronly = true})

    on_install(function (package)
        os.cp("clear_all_cpp_std_headers.h", package:installdir("include"))
    end)

    on_test(function (package)
        assert(package:has_cxxincludes("clear_all_cpp_std_headers.h"))
    end)
package_end()
