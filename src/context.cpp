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

    namespace detail
    {
        extern "C++"
        {
            /// 创建VCD波形文件记录器
            ::std::unique_ptr<::VerilatedVcdC> create_tracer_vcd();

            /// 创建FST波形文件记录器
            ::std::unique_ptr<::VerilatedFstC> create_tracer_fst();

            /// 创建SAIF波形文件记录器
            ::std::unique_ptr<::VerilatedSaifC> create_tracer_saif();
        }

        template <::verilator_utils::is_verilator_tracer tracer_t>
            requires (!::std::same_as<tracer_t, void>)
        inline ::std::unique_ptr<tracer_t> create_tracer()
        {
            if constexpr(::std::same_as<::VerilatedVcdC, tracer_t>) { return ::verilator_utils::detail::create_tracer_vcd(); }
            else if constexpr(::std::same_as<::VerilatedFstC, tracer_t>)
            {
                return ::verilator_utils::detail::create_tracer_fst();
            }
            else if constexpr(::std::same_as<::VerilatedSaifC, tracer_t>)
            {
                return ::verilator_utils::detail::create_tracer_saif();
            }
        }

    }  // namespace detail

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

    public:
        /**
         * @brief 构造一个DUT上下文对象
         *
         * @param time_unit 时间单位，默认值为ns
         * @param time_precision 时间精度，默认值为ps
         * @param argc 命令行参数数量，默认为detail::argc
         * @param argv 命令行参数数组，默认为detail::argv
         * @param trace_level 跟踪级别，默认值为0
         */
        inline dut_context(::verilator_utils::verilator_time_unit time_unit = ::verilator_utils::verilator_time_unit::ns,
                           ::verilator_utils::verilator_time_unit time_precision = ::verilator_utils::verilator_time_unit::ps,
                           int argc = ::verilator_utils::detail::argc,
                           const char** argv = ::verilator_utils::detail::argv,
                           int trace_level = 0)
        {
            auto&& current_test{*::doctest::getContextOptions()->currentTest};
            context = ::std::make_unique<::VerilatedContext>();
            dut = ::std::make_unique<dut_t>(context.get(), current_test.m_test_suite);
            context->timeprecision(::std::to_underlying(time_precision));
            context->timeunit(::std::to_underlying(time_unit));
            context->commandArgs(argc, argv);
            scheduler = ::std::make_unique<::verilator_utils::eval_scheduler>(*dut);

            if constexpr(!::std::same_as<tracer_t, void>)
            {
                context->traceEverOn(true);
                // tracer = ::std::make_unique<tracer_t>();
                tracer = ::verilator_utils::detail::create_tracer<tracer_t>();
                dut->trace(tracer.get(), trace_level);
                if constexpr(::std::same_as<tracer_t, ::VerilatedVcdC>)
                {
                    tracer->open(::std::format("{}.vcd", current_test.m_name).data());
                }
                else if constexpr(::std::same_as<tracer_t, ::VerilatedFstC>)
                {
                    tracer->open(::std::format("{}.fst", current_test.m_name).data());
                }
                else if constexpr(::std::same_as<tracer_t, ::VerilatedSaifC>)
                {
                    tracer->open(::std::format("{}.saif", current_test.m_name).data());
                }
            }
        }

        inline ~dut_context() noexcept { dut->final(); }

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
         */
        inline void initial_eval()
        {
            scheduler->initial_eval();
            if constexpr(!::std::same_as<tracer_t, void>) { tracer->dump(context->time()); }
        }

        /**
         * @brief 执行初始化循环，然后执行调度器循环直到调度器队列为空或者仿真结束，如果启用波形记录器，则记录波形
         *
         */
        inline void loop_until_finish()
        {
            initial_eval();
            while(!scheduler->empty() && !scheduler->is_finish()) { loop_once(); }
        }
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
