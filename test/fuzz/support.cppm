export module fuzz_support;
export import verilator_utils.full;

namespace fuzz::detail
{
    /**
     * @brief 输出诊断信息
     *
     * @tparam args_t 格式化参数类型
     * @param fmt 格式化字符串
     * @param args 格式化参数
     * @note 诊断信息输出失败时（格式化异常或写入异常）直接终止进程
     */
    template <typename... args_t>
    void report(::std::format_string<args_t...> fmt, args_t&&... args) noexcept
    {
        try
        {
            ::std::cerr << ::std::format(fmt, ::std::forward<args_t>(args)...);
        }
        catch(...)
        {
            ::std::abort();
        }
    }
}  // namespace fuzz::detail

/// 模糊测试回调函数类型
using fuzz_callback_t = int(const ::std::uint8_t* data, ::std::size_t size);
extern "C" int LLVMFuzzerRunDriver(int* argc, char*** argv, ::fuzz_callback_t* callback);

export namespace fuzz
{
    using ::fuzz_callback_t;

    /**
     * @brief 字节流解码器
     *
     * 越界读取返回0，保证任何输入都能被解码而不会越界
     */
    class reader
    {
    public:
        reader(const ::std::uint8_t* data, ::std::size_t size) noexcept : data_{data, size} {}

        /// 是否已耗尽输入
        [[nodiscard]] bool empty() const noexcept { return index_ >= data_.size(); }

        /// 读取1字节
        [[nodiscard]] ::std::uint8_t u8() noexcept { return index_ < data_.size() ? data_[index_++] : 0; }

        /// 读取任意大小的无符号整数，按小端序解码
        [[nodiscard]] ::std::uint64_t uint(::std::size_t bytes) noexcept
        {
            ::std::uint64_t result{};
            for(::std::size_t i{}; i != bytes && i != 8; ++i) { result |= static_cast<::std::uint64_t>(u8()) << (i * 8); }
            return result;
        }

        /// 读取8字节无符号整数
        [[nodiscard]] ::std::uint64_t u64() noexcept { return uint(8); }

        /// 读取指定范围内的无符号整数
        [[nodiscard]] ::std::uint64_t range(::std::uint64_t begin, ::std::uint64_t end) noexcept
        { return end <= begin ? begin : begin + u64() % (end - begin + 1); }

        /// 读取布尔值
        [[nodiscard]] bool boolean() noexcept { return (u8() & 1u) != 0; }

        /// 读取一个短字符串，字符取自可打印ASCII范围
        [[nodiscard]] ::std::string short_string(::std::size_t max_size)
        {
            const auto size{static_cast<::std::size_t>(range(1, max_size))};
            ::std::string result{};
            result.reserve(size);
            for(::std::size_t i{}; i != size; ++i) { result.push_back(static_cast<char>('a' + u8() % 26)); }
            return result;
        }

    private:
        ::std::span<const ::std::uint8_t> data_;
        ::std::size_t index_{};
    };

    /**
     * @brief 报告不变量被破坏并终止进程
     *
     * @param message 描述信息
     * @note 使用abort而不是抛出异常，使libFuzzer可以记录输入并生成崩溃报告
     */
    [[noreturn]] void fail(::std::string_view message) noexcept
    {
        ::fuzz::detail::report("fuzz: invariant violated: {}\n", message);
        ::std::abort();
    }

    /**
     * @brief 报告不变量被破坏并终止进程
     *
     * @param message 描述信息
     * @param detail 附加的整数值
     */
    [[noreturn]] void fail(::std::string_view message, ::std::uint64_t detail) noexcept
    {
        ::fuzz::detail::report("fuzz: invariant violated: {} (detail={})\n", message, detail);
        ::std::abort();
    }

    /**
     * @brief 检查不变量
     *
     * @param condition 不变量
     * @param location 调用处的位置
     */
    void check(bool condition, ::std::source_location location = ::std::source_location::current()) noexcept
    {
        if(condition) [[likely]] { return; }
        ::fuzz::detail::report("fuzz: invariant violated at {}:{}: {}\n",
                               location.file_name(),
                               location.line(),
                               location.function_name());
        ::std::abort();
    }

    /**
     * @brief 检查不变量
     *
     * @param condition 不变量
     * @param detail 不成立时输出的附加整数值
     * @param location 调用处的位置
     */
    void check(bool condition,
               ::std::uint64_t detail,
               ::std::source_location location = ::std::source_location::current()) noexcept
    {
        if(condition) [[likely]] { return; }
        ::fuzz::detail::report("fuzz: invariant violated at {}:{} (detail={})\n", location.file_name(), location.line(), detail);
        ::std::abort();
    }

    /**
     * @brief 关闭断言的栈回溯
     *
     * 模糊测试中异常路径占主导，生成栈回溯会显著拖慢执行速度
     */
    void disable_stacktrace() noexcept
    {
        auto config{::verilator_utils::assertion_config()};
        config.stacktrace = false;
        ::verilator_utils::set_assertion_config(config);
    }

    /**
     * @brief 运行模糊测试
     *
     * @param callback 模糊测试回调函数
     */
    void run_fuzz_test(::fuzz_callback_t* callback)
    {
        const auto args{::verilator_utils::detail::dut_context_default_args::args};
        auto argc{static_cast<int>(args.size())};
        auto* argv{args.data()};
        ::LLVMFuzzerRunDriver(&argc, &argv, callback);
    }
}  // namespace fuzz

/**
 * @brief libFuzzer入口，在模糊测试开始前调用
 *
 * @note libFuzzer通过弱引用查找该符号，只要链接了本模块就会被调用
 * @param argc 命令行参数个数
 * @param argv 命令行参数
 * @return 0表示初始化成功
 */
extern "C" int LLVMFuzzerInitialize(int* /*argc*/, char*** /*argv*/)
{
    // 模糊测试中异常路径占主导，关闭断言的栈回溯以提升执行速度
    ::fuzz::disable_stacktrace();
    return 0;
}
