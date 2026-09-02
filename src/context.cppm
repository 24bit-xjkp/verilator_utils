module;
#include <assert_macros.hpp>
#include <doctest_macros.hpp>
export module verilator_utils:context;
import :task;

extern "C++" int main(int, const char**);

namespace
{
    using namespace ::std::string_view_literals;
}

export namespace verilator_utils
{
    /**
     * @brief 判断类型是否是受支持的Verilator波形记录器
     *
     * 支持VCD、FST、SAIF，为void表示不使用波形记录
     * @tparam type 要判断的类型
     */
    template <typename type>
    concept is_verilator_tracer = ::verilator_utils::same_as_any<type, ::VerilatedVcdC, ::VerilatedFstC, ::VerilatedSaifC, void>;

    template <::std::derived_from<::VerilatedModel> dut_t, ::verilator_utils::is_verilator_tracer tracer_t>
    struct dut_context;

    extern "C++" namespace detail
    {
        /**
         * @brief DUT上下文使用的默认命令行参数
         *
         */
        struct dut_context_default_args
        {
        private:
            template <::std::derived_from<::VerilatedModel> dut_t, ::verilator_utils::is_verilator_tracer tracer_t>
            friend struct ::verilator_utils::dut_context;
            friend int ::main(int, const char**);

            static int argc;
            static const char** argv;
        };

        constinit int ::verilator_utils::detail::dut_context_default_args::argc{};
        constinit const char** ::verilator_utils::detail::dut_context_default_args::argv{};
    }  // namespace detail
}  // namespace verilator_utils

export namespace verilator_utils
{
    /**
     * @brief DUT上下文配置参数
     *
     */
    struct dut_context_option
    {
        /// 是否启用覆盖率记录
        bool coverage{};
        /// 时间单位，默认值为ns，会覆盖dut内设置
        ::verilator_utils::verilator_time_unit time_unit{::verilator_utils::verilator_time_unit::ns};
        /// 时间精度，默认值为ps，会覆盖dut内设置
        ::verilator_utils::verilator_time_unit time_precision{::verilator_utils::verilator_time_unit::ps};
        /// 生成文件的基本名称，不带有后缀名，默认为doctest的测试用例名称
        ::std::string_view base_name{};
        /// 跟踪级别，默认值为0
        int trace_level{};
        /// 命令行参数数量，默认为传递给程序的命令行参数数量，不进行过滤
        ::std::optional<int> argc{};
        /// 命令行参数数组，默认为传递给程序的命令行参数数组，不进行过滤
        ::std::optional<const char**> argv{};
    };

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
        constexpr static auto use_tracer{!::std::is_void_v<tracer_t>};
        // 若不使用波形记录器，则使用std::size_t占位
        using actual_tracer_t = ::std::conditional_t<use_tracer, tracer_t, ::std::size_t>;
        ::std::unique_ptr<actual_tracer_t> tracer{};
        bool coverage{};
        ::std::string file_base_name{};

        /**
         * @brief 在doctest断言失败时记录随机种子
         *
         */
        struct log_random_seed
        {
            const ::VerilatedContext* context;

            void operator() (::std::ostream* stream) const
            {
                constexpr auto location{::std::source_location::current()};
                ::doctest::detail::MessageBuilder msg_builder{location.file_name(),
                                                              location.line(),
                                                              ::doctest::assertType::is_warn};
                msg_builder.m_stream = stream;
                msg_builder* ::std::format("random_seed := {}"sv, static_cast<::std::size_t>(context->randSeed()));
            }
        };

        ::std::optional<::doctest::detail::ContextScope<log_random_seed>> random_seed_logger{};

    public:
        /**
         * @brief 构造一个DUT上下文对象
         *
         * @param option 配置选项
         * @note 记录文件会在initial_eval时才打开
         */
        explicit dut_context(::verilator_utils::dut_context_option option = {}) : coverage{option.coverage}
        {
            // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer)
            auto&& current_test{*::doctest::getContextOptions()->currentTest};
            context = ::std::make_unique<::VerilatedContext>();
            context->commandArgs(option.argc.value_or(::verilator_utils::detail::dut_context_default_args::argc),
                                 option.argv.value_or(::verilator_utils::detail::dut_context_default_args::argv));
            dut = ::std::make_unique<dut_t>(context.get(),
                                            current_test.m_test_suite == nullptr ? "TOP" : current_test.m_test_suite);
            // 覆盖dut内的timescale设置
            context->timeprecision(::std::to_underlying(option.time_precision));
            context->timeunit(::std::to_underlying(option.time_unit));
            scheduler = ::std::make_unique<::verilator_utils::eval_scheduler>(*dut);
            file_base_name = option.base_name.empty() ? current_test.m_name : option.base_name;

            if constexpr(use_tracer)
            {
                context->traceEverOn(true);
                if constexpr(::std::same_as<::VerilatedVcdC, tracer_t>) { tracer = ::std::make_unique<::VerilatedVcdC>(); }
                else if constexpr(::std::same_as<::VerilatedFstC, tracer_t>) { tracer = ::std::make_unique<::VerilatedFstC>(); }
                else if constexpr(::std::same_as<::VerilatedSaifC, tracer_t>) { tracer = ::std::make_unique<::VerilatedSaifC>(); }
                dut->trace(tracer.get(), option.trace_level);
            }
            // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
        }

        dut_context(const dut_context&) = delete;
        dut_context& operator= (const dut_context&) = delete;
        // random_seed_logger 注册到 doctest 的上下文作用域栈中，移动会破坏其LIFO要求，因此禁止移动
        dut_context(dut_context&&) noexcept = delete;
        dut_context& operator= (dut_context&&) noexcept = delete;

        ~dut_context() noexcept
        {
            dut->final();
            if(coverage && scheduler->get_eval_stage() != ::verilator_utils::eval_scheduler::eval_stage_enum::not_begin)
            {
                context->coverageFilename(::std::format("{}.dat"sv, file_base_name));
                context->coveragep()->write();
            }
        }

        /**
         * @brief 执行一次调度器循环，如果启用波形记录器，则记录波形
         *
         */
        void loop_once()
        {
            scheduler->loop_once();
            if constexpr(use_tracer) { tracer->dump(context->time()); }
        }

        /**
         * @brief 执行调度器的初始化循环，如果启用波形记录器，则记录波形
         *
         * @note 会创建波形记录文件
         */
        void initial_eval()
        {
            if constexpr(::std::same_as<tracer_t, ::VerilatedVcdC>)
            {
                tracer->open(::std::format("{}.vcd"sv, file_base_name).data());
            }
            else if constexpr(::std::same_as<tracer_t, ::VerilatedFstC>)
            {
                tracer->open(::std::format("{}.fst"sv, file_base_name).data());
            }
            else if constexpr(::std::same_as<tracer_t, ::VerilatedSaifC>)
            {
                tracer->open(::std::format("{}.saif"sv, file_base_name).data());
            }

            scheduler->initial_eval();
            if constexpr(use_tracer) { tracer->dump(context->time()); }
        }

        /**
         * @brief 获取当前上下文中生成文件的基本名称，不带后缀名
         *
         * @return 文件基本名称
         */
        [[nodiscard]] ::std::string_view get_base_name() const noexcept { return file_base_name; }

        /**
         * @brief 设置生成文件的基本名称，不带后缀名
         *
         * @note 由于initial_eval会创建记录文件，因此必须在initial_eval前设置
         * @param base_name 文件基本名称
         */
        void set_base_name(::std::string_view base_name)
        {
            VU_CHECK(scheduler->get_eval_stage() == ::verilator_utils::eval_scheduler::eval_stage_enum::not_begin,
                     "必须在initial_eval之前设置文件基本名称"sv);
            file_base_name = base_name;
        }

        /**
         * @brief 获取Verilator上下文对象引用
         *
         * @return Verilator上下文对象引用
         */
        auto&& get_context(this auto&& self) noexcept { return *self.context; }

        /**
         * @brief 获取DUT对象引用
         *
         * @return DUT对象引用
         */
        auto&& get_dut(this auto&& self) noexcept { return *self.dut; }

        /**
         * @brief 获取调度器对象引用
         *
         * @return 调度器对象引用
         */
        auto&& get_scheduler(this auto&& self) noexcept { return *self.scheduler; }

        /**
         * @brief 获取跟踪器引用
         *
         * 只有当tracer_t不为void，即启用跟踪器时可调用
         * @return 跟踪器引用
         */
        auto&& get_tracer(this auto&& self) noexcept
            requires (use_tracer)
        { return *self.tracer; }

        /**
         * @brief 获取VerilatorContext的随机种子
         *
         * @return 随机种子
         */
        ::std::size_t get_seed() noexcept
        {
            if(!random_seed_logger.has_value())
            {
                random_seed_logger.emplace(::doctest::detail::MakeContextScope(log_random_seed{context.get()}));
            }
            return static_cast<::std::size_t>(context->randSeed());
        }

        /**
         * @brief 判断当前上下文中覆盖率记录是否启用
         *
         * @return 覆盖率记录是否启用
         */
        [[nodiscard]] bool is_coverage_enabled() const noexcept { return coverage; }

        /**
         * @brief 设置覆盖率记录是否启用
         *
         * @param enable_coverage 覆盖率记录是否启用
         */
        void set_coverage_status(bool enable_coverage) noexcept { coverage = enable_coverage; }

        /**
         * @brief 执行初始化循环，然后执行调度器循环直到调度器队列为空或者仿真结束，如果启用波形记录器，则记录波形
         *
         * @param max_eval_time 最大仿真时长，为0表示无限制
         * @note 会创建波形记录文件
         */
        void loop_until_finish(::verilator_utils::femtosecond_t max_eval_time = 0_fs)
        {
            if(max_eval_time != 0_fs) { add_task(::verilator_utils::max_eval_time(max_eval_time)); }
            initial_eval();
            while(!scheduler->empty() && !scheduler->is_finish()) { loop_once(); }
        }

        /**
         * @brief 向调度器中添加任务
         *
         * @param task 要添加的任务
         * @note 相当于在绑定的调度器对象scheduler上调用add_task
         */
        void add_task(::verilator_utils::task<void> task) noexcept { scheduler->add_task(::std::move(task)); }
    };

}  // namespace verilator_utils
