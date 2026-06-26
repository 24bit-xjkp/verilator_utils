package("clean_std_heads")
    set_homepage("https://github.com/YexuanXiao/convert-cpp-std-headers-to-std-module")
    set_urls("https://github.com/YexuanXiao/convert-cpp-std-headers-to-std-module/archive/refs/tags/$(version).zip")
    add_versions("2026-06-26", "7ab33949aed983c8759a488b53bde686dc2d4f8d3512a6c57d45afa1737fd305")
    set_kind("library", {headeronly = true})

    on_install(function (package)
        os.cp("clear_all_cpp_std_headers.h", package:installdir("include"))
    end)

    on_test(function (package)
        assert(package:has_cxxincludes("clear_all_cpp_std_headers.h"))
    end)
package_end()
