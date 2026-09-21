module;
#include <verilated_config.h>
export module unit_test;
export import verilator_utils.full;

export namespace verilator_utils
{
    // 简化单元测试内使用
    using ::verilator_utils::detail::check;
}  // namespace verilator_utils

export {
    using namespace ::verilator_utils;
    using namespace ::verilator_utils::verilator;
    using namespace ::std::string_view_literals;
}

struct verilator_version_t
{
    /// 主版本号
    ::std::size_t major{};
    /// 次版本号
    ::std::size_t minor{};

    /**
     * @brief 解析Verilator版本字符串
     *
     * @param version 版本字符串，如5.052
     * @code {.cpp}
     * constexpr auto [major, minor]{verilator_version_t{"5.052"sv}};
     * static_assert(major == 5 && minor == 52);
     * @endcode
     */
    constexpr explicit verilator_version_t(::std::string_view version)
    {
        const char* iter{version.begin()};
        const char* end{version.end()};
        const auto result{::std::from_chars(iter, end, major)};
        if(!result || *result.ptr != '.') { throw ::std::invalid_argument{"版本解析失败"}; }
        iter = ::std::next(result.ptr, 1);
        if(!::std::from_chars(iter, end, minor)) { throw ::std::invalid_argument{"版本解析失败"}; }
    }

    constexpr friend bool operator== (verilator_version_t lhs, verilator_version_t rhs) noexcept = default;
    constexpr friend auto operator<=> (verilator_version_t lhs, verilator_version_t rhs) noexcept = default;
};

template <verilator_version_t version>
struct dummy_verilated_model : ::VerilatedModel
{
    explicit dummy_verilated_model(::VerilatedContext& context) : ::VerilatedModel{context} {}
};

/// 提供一个与VerilatedModel构造函数兼容的空基类
struct dummy_verilated_model_base
{
    explicit dummy_verilated_model_base(::VerilatedContext& /* unused */) noexcept {}
};

/// 将基类选择推迟到模板实例化时，以便重写一些可能不存在的虚函数
template <bool satisfy>
using select_base = ::std::conditional_t<satisfy, ::VerilatedModel, ::dummy_verilated_model_base>;

constexpr verilator_version_t v5_052{"5.052"sv};

template <verilator_version_t version>
    requires (version >= ::v5_052)
struct dummy_verilated_model<version> : ::select_base<version >= ::v5_052>
{
    explicit dummy_verilated_model(::VerilatedContext& context) : ::select_base<version >= ::v5_052>{context} {}

    // Verilator v5.052新增了如下纯虚函数，为其添加默认实现

private:
    void evalBegin() override {}

    void evalEnd() override {}

    void evalStatic() override {}

    void evalInitial() override {}

    void evalSample() override {}

    bool evalStl(bool /* unused */) override { return false; }

    bool evalIco(bool /* unused */) override { return false; }

    bool evalAct() override { return false; }

    bool evalInact() override { return false; }

    bool evalNba() override { return false; }

    bool evalObs() override { return false; }

    bool evalReact() override { return false; }

    void evalPostponed() override {}

    void dumpTriggersStl() override {}

    void dumpTriggersIco() override {}

    void dumpTriggersAct() override {}

    void dumpTriggersNba() override {}

    void dumpTriggersObs() override {}

    void dumpTriggersReact() override {}

    void evalFinal() override {}
};

export {
    constexpr verilator_version_t verilator_version{VERILATOR_VERSION};

    struct fake_dut : ::dummy_verilated_model<::verilator_version>
    {
        explicit fake_dut(::VerilatedContext& context) : ::dummy_verilated_model<::verilator_version>{context} {}

        void eval() {}

        [[nodiscard]] const char* hierName() const override { return "fake_dut"; }

        [[nodiscard]] const char* modelName() const override { return "fake_dut"; }

        [[nodiscard]] unsigned threads() const override { return 1u; }

        void prepareClone() const { contextp()->prepareClone(); }

        void atClone() const { contextp()->threadPoolpOnClone(); }
    };

    /// 测试用单比特信号
    struct signal_state
    { ::CData value{}; };

    /**
     * @brief 协程帧析构计数器
     *
     * 作为协程体中的局部对象使用：协程帧被销毁时，帧内局部对象的析构函数会执行。
     * 通过统计析构次数即可观察调度器在析构时是否回收了挂起在事件或等待队列上的协程帧，
     * 以及被等待或被取消的异步任务是否回收了协程帧
     *
     * @note 用法限定为在协程帧或栈上就地构造的RAII计数器：复制会多出一个析构点、
     *       移动会让移出对象仍然自增，二者都会破坏"每个计数器恰好自增一次"的前提，
     *       因此显式删除全部复制与移动操作
     */
    struct frame_destruction_counter
    {
        ::std::size_t* count;

        explicit frame_destruction_counter(::std::size_t* count) noexcept : count{count} {}

        frame_destruction_counter(const frame_destruction_counter&) = delete;
        frame_destruction_counter& operator= (const frame_destruction_counter&) = delete;
        frame_destruction_counter(frame_destruction_counter&&) = delete;
        frame_destruction_counter& operator= (frame_destruction_counter&&) = delete;

        ~frame_destruction_counter() noexcept { ++*count; }
    };

    /// 调度器测试夹具：提供已配置时间单位与精度的仿真上下文
    struct scheduler_fixture
    {
        ::VerilatedContext context{};
        ::fake_dut dut{context};

        /// @param time_unit 时间单位（负指数，-9表示ns）
        /// @param time_precision 时间精度（负指数，-12表示ps）
        /// @note eval_scheduler在构造时缓存时间单位与精度，因此必须先配置VerilatedContext再创建调度器
        explicit scheduler_fixture(::std::int32_t time_unit = -9, ::std::int32_t time_precision = -12)
        {
            context.timeunit(time_unit);
            context.timeprecision(time_precision);
        }

        [[nodiscard]] ::verilator_utils::eval_scheduler make_scheduler() noexcept
        { return ::verilator_utils::eval_scheduler{dut}; }
    };
}
