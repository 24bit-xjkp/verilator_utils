import verilator_utils.full;

int main(int argc, const char* argv[])
{
    ::verilator_utils::detail::set_console_utf8 _{};
    ::verilator_utils::detail::dut_context_default_args::argc = argc;
    ::verilator_utils::detail::dut_context_default_args::argv = argv;
    try
    {
        return ::doctest::Context{argc, argv}.run();
    }
    catch(...)
    {
        ::std::terminate();
    }
}
