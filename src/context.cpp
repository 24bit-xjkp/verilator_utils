module;
#include <doctest_fwd.hpp>
export module verilator_utils:context;
import :scheduler;

extern "C++"
{
#include <doctest.h>
}

export namespace verilator_utils
{
    /**
     * @brief 判断类型是否是受支持的Verilator波形记录器
     *
     * @note 支持VCD、FST、SAIF，为void表示不使用波形记录
     * @tparam type 要判断的类型
     */
    template <typename type>
    concept is_verilator_tracer = ::std::same_as<::VerilatedVcdC, type> || ::std::same_as<::VerilatedFstC, type> ||
                                  ::std::same_as<::VerilatedSaifC, type> || ::std::same_as<void, type>;

    /**
     * @brief DUT上下文类型
     *
     * @tparam dut_t DUT类型，必须是VerilatedModel的派生类
     * @tparam tracer_t 波形记录器类型
     */
    template <::std::derived_from<::VerilatedModel> dut_t, ::verilator_utils::is_verilator_tracer tracer_t>
    struct dut_context
    {
    private:
        ::std::unique_ptr<::VerilatedContext> context{};
        ::std::unique_ptr<dut_t> dut{};
        ::std::unique_ptr<::verilator_utils::eval_scheduler> scheduler{};
        // 若不使用波形记录器，则使用std::size_t占位
        using actual_tracer_t = ::std::conditional_t<::std::same_as<tracer_t, void>, ::std::size_t, tracer_t>;
        ::std::unique_ptr<actual_tracer_t> tracer{};
        bool coverage{};
        ::std::string file_base_name{};

    public:
        /**
         * @brief 构造一个DUT上下文对象
         *
         * @note 记录文件会在initial_eval时才打开
         * @param coverage 是否启用覆盖率记录
         * @param time_unit 时间单位，默认值为ns，会覆盖dut内设置
         * @param time_precision 时间精度，默认值为ps，会覆盖dut内设置
         * @param base_name 生成文件的基本名称，不带有后缀名，默认为doctest的测试用例名称
         * @param trace_level 跟踪级别，默认值为0
         * @param argc 命令行参数数量，默认为detail::argc
         * @param argv 命令行参数数组，默认为detail::argv
         */
        inline dut_context(bool coverage,
                           ::verilator_utils::verilator_time_unit time_unit = ::verilator_utils::verilator_time_unit::ns,
                           ::verilator_utils::verilator_time_unit time_precision = ::verilator_utils::verilator_time_unit::ps,
                           ::std::string_view base_name = ::std::string_view{},
                           int trace_level = 0,
                           int argc = ::verilator_utils::detail::argc,
                           const char** argv = ::verilator_utils::detail::argv) : coverage{coverage}
        {
            // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer)
            auto&& current_test{*::doctest::getContextOptions()->currentTest};
            context = ::std::make_unique<::VerilatedContext>();
            context->commandArgs(argc, argv);
            dut = ::std::make_unique<dut_t>(context.get(), current_test.m_test_suite);
            // 覆盖dut内的timescale设置
            context->timeprecision(::std::to_underlying(time_precision));
            context->timeunit(::std::to_underlying(time_unit));
            scheduler = ::std::make_unique<::verilator_utils::eval_scheduler>(*dut);
            file_base_name = base_name.empty() ? current_test.m_name : base_name;

            if constexpr(!::std::same_as<tracer_t, void>)
            {
                context->traceEverOn(true);
                if constexpr(::std::same_as<::VerilatedVcdC, tracer_t>) { tracer = ::std::make_unique<::VerilatedVcdC>(); }
                else if constexpr(::std::same_as<::VerilatedFstC, tracer_t>) { tracer = ::std::make_unique<::VerilatedFstC>(); }
                else if constexpr(::std::same_as<::VerilatedSaifC, tracer_t>) { tracer = ::std::make_unique<::VerilatedSaifC>(); }
                dut->trace(tracer.get(), trace_level);
            }
            // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
        }

        inline dut_context(const dut_context&) = delete;
        inline dut_context& operator= (const dut_context&) = delete;
        inline dut_context(dut_context&&) noexcept = default;
        inline dut_context& operator= (dut_context&&) noexcept = default;

        inline ~dut_context() noexcept
        {
            dut->final();
            if(coverage && scheduler->get_eval_stage() != ::verilator_utils::eval_scheduler::eval_stage_enum::not_begin)
            {
                context->coverageFilename(::std::format("{}.dat", file_base_name));
                context->coveragep()->write();
            }
        }

        /**
         * @brief DUT上下文对象中子组件的数量
         *
         * @return 子组件数量
         */
        constexpr inline static ::std::size_t components() noexcept { return ::std::same_as<tracer_t, void> ? 3 : 4; }

        /**
         * @brief 获取DUT上下文对象的子组件
         *
         * @tparam index 子组件索引，0为上下文对象，1为DUT对象，2为跟踪器对象
         * @return 子组件的引用
         */
        template <::std::size_t index>
            requires (index < components())
        inline auto&& get() noexcept
        {
            if constexpr(index == 0) { return *context; }
            else if constexpr(index == 1) { return *dut; }
            else if constexpr(index == 2) { return *scheduler; }
            else if constexpr(index == 3) { return *tracer; }
        }

        /**
         * @brief 执行一次调度器循环，如果启用波形记录器，则记录波形
         *
         */
        inline void loop_once()
        {
            scheduler->loop_once();
            if constexpr(!::std::same_as<tracer_t, void>) { tracer->dump(context->time()); }
        }

        /**
         * @brief 执行调度器的初始化循环，如果启用波形记录器，则记录波形
         *
         * @note 会创建波形记录文件
         */
        inline void initial_eval()
        {
            if constexpr(::std::same_as<tracer_t, ::VerilatedVcdC>)
            {
                tracer->open(::std::format("{}.vcd", file_base_name).data());
            }
            else if constexpr(::std::same_as<tracer_t, ::VerilatedFstC>)
            {
                tracer->open(::std::format("{}.fst", file_base_name).data());
            }
            else if constexpr(::std::same_as<tracer_t, ::VerilatedSaifC>)
            {
                tracer->open(::std::format("{}.saif", file_base_name).data());
            }

            scheduler->initial_eval();
            if constexpr(!::std::same_as<tracer_t, void>) { tracer->dump(context->time()); }
        }

        /**
         * @brief 获取当前上下文中生成文件的基本名称，不带后缀名
         *
         * @return 文件基本名称
         */
        [[nodiscard]] inline ::std::string_view get_base_name() const noexcept { return file_base_name; }

        /**
         * @brief 设置生成文件的基本名称，不带后缀名
         *
         * @note 由于initial_eval会创建记录文件，因此必须在initial_eval前设置
         * @param base_name 文件基本名称
         */
        inline void set_base_name(::std::string_view base_name)
        {
            REQUIRE_EQ(scheduler->get_eval_stage(), ::verilator_utils::eval_scheduler::eval_stage_enum::not_begin);
            file_base_name = base_name;
        }

        /**
         * @brief 判断当前上下文中覆盖率记录是否启用
         *
         * @return 覆盖率记录是否启用
         */
        [[nodiscard]] inline bool is_coverage_enabled() const noexcept { return coverage; }

        /**
         * @brief 设置覆盖率记录是否启用
         *
         * @param enable_coverage 覆盖率记录是否启用
         */
        inline void set_coverage_status(bool enable_coverage) noexcept { coverage = enable_coverage; }

        /**
         * @brief 执行初始化循环，然后执行调度器循环直到调度器队列为空或者仿真结束，如果启用波形记录器，则记录波形
         *
         * @note 会创建波形记录文件
         */
        inline void loop_until_finish()
        {
            initial_eval();
            while(!scheduler->empty() && !scheduler->is_finish()) { loop_once(); }
        }

        /**
         * @brief 向调度器中添加任务
         *
         * @param task 要添加的任务
         * @note 相当于在绑定的调度器对象scheduler上调用add_task
         */
        inline void add_task(::verilator_utils::task task) noexcept { scheduler->add_task(::std::move(task)); }
    };
}  // namespace verilator_utils

export namespace std
{
    template <::std::derived_from<::VerilatedModel> dut_t, ::verilator_utils::is_verilator_tracer tracer_t>
    struct tuple_size<::verilator_utils::dut_context<dut_t, tracer_t>> :
        ::std::integral_constant<::std::size_t, ::verilator_utils::dut_context<dut_t, tracer_t>::components()>
    {
    };

    template <::std::derived_from<::VerilatedModel> dut_t, ::verilator_utils::is_verilator_tracer tracer_t>
    struct tuple_element<0, ::verilator_utils::dut_context<dut_t, tracer_t>>
    {
        using type = ::VerilatedContext&;
    };

    template <::std::derived_from<::VerilatedModel> dut_t, ::verilator_utils::is_verilator_tracer tracer_t>
    struct tuple_element<1, ::verilator_utils::dut_context<dut_t, tracer_t>>
    {
        using type = dut_t&;
    };

    template <::std::derived_from<::VerilatedModel> dut_t, ::verilator_utils::is_verilator_tracer tracer_t>
    struct tuple_element<2, ::verilator_utils::dut_context<dut_t, tracer_t>>
    {
        using type = ::verilator_utils::eval_scheduler&;
    };

    template <::std::derived_from<::VerilatedModel> dut_t, ::verilator_utils::is_verilator_tracer tracer_t>
    struct tuple_element<3, ::verilator_utils::dut_context<dut_t, tracer_t>>
    {
        using type = tracer_t&;
    };
}  // namespace std
