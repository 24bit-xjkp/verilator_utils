import verilator_utils.full;

int main(int argc, const char* argv[])
{
    ::verilator_utils::detail::set_console_utf8 _{};
    ::verilator_utils::detail::dut_context_default_args::argc = argc;
    ::verilator_utils::detail::dut_context_default_args::argv = argv;
    try
    {
        ::doctest::Context context{argc, argv};
        context.setAsDefaultForAssertsOutOfTestCases();
        if(const auto* options{::doctest::getContextOptions()}; options != nullptr)
        {
            // 复用doctest的--force-colors/--no-colors命令行选项
            ::verilator_utils::set_assertion_color_config(
                {.force_colors = options->force_colors, .no_colors = options->no_colors});
        }
        return context.run();
    }
    catch(...)
    {
        ::std::terminate();
    }
}
