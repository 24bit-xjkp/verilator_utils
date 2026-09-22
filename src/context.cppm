module;
#include <verilated_config.h>
#include <doctest_macros.hpp>
export module verilator_utils:context;
import :task;

extern "C++" int main(int, const char*[]);

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
        struct dut_context_default_args  // NOLINT(misc-use-internal-linkage)
        {
        private:
            template <::std::derived_from<::VerilatedModel> dut_t, ::verilator_utils::is_verilator_tracer tracer_t>
            friend struct ::verilator_utils::dut_context;
            friend int ::main(int, const char*[]);

            static ::std::span<const char*> args;
        };

        constinit ::std::span<const char*> verilator_utils::detail::dut_context_default_args::args{};
    }  // namespace detail
}  // namespace verilator_utils

namespace verilator_utils::detail
{
    template <::verilator_utils::is_verilator_tracer tracer_t>
    struct dut_context_tracer
    {
        ::std::unique_ptr<tracer_t> tracer{};

        void init() { tracer = ::std::make_unique<tracer_t>(); }

        void open(::std::string_view base_name)
        {
            constexpr ::std::string_view format_string{[] static consteval noexcept {
                if constexpr(::std::same_as<tracer_t, ::VerilatedVcdC>) { return "{}.vcd"sv; }
                else if constexpr(::std::same_as<tracer_t, ::VerilatedFstC>) { return "{}.fst"sv; }
                else if constexpr(::std::same_as<tracer_t, ::VerilatedSaifC>) { return "{}.saif"sv; }
                else
                {
                    static_assert(false, "不支持的跟踪器类型");
                }
            }()};
            tracer->open(::std::format(format_string, base_name).data());
        }

        tracer_t* get() const noexcept { return tracer.get(); }

        void dump(::std::uint64_t time) { tracer->dump(time); }
    };

    template <>
    struct dut_context_tracer<void>
    {
    };
}  // namespace verilator_utils::detail

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
     * @brief DUT上下文统计信息
     *
     */
    struct dut_context_stats
    {
        /// 仿真器名称
        constexpr static ::std::string_view stimulator{VERILATOR_PRODUCT " " VERILATOR_VERSION};
        /// 仿真时间
        ::std::string simtime_str;
        /// 仿真速度
        ::std::string speed_str;
        /// 并发线程数
        ::std::size_t threads;
        /// 仿真时间，单位为dut时间单位
        double simtime;
        /// 墙钟时间，以秒为单位
        double walltime;
        /// cpu时间，以秒为单位
        double cputime;
        /// 仿真速度，单位为dut时间单位/s
        double speed;
        /// 峰值内存，单位为MB
        double memory_peak;
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
        ::std::unique_ptr<::VerilatedContext> context_{};
        ::std::unique_ptr<dut_t> dut_{};
        ::std::unique_ptr<::verilator_utils::eval_scheduler> scheduler_{};
        constexpr static auto use_tracer{!::std::is_void_v<tracer_t>};
        [[no_unique_address]] ::verilator_utils::detail::dut_context_tracer<tracer_t> tracer_{};
        bool coverage_{};
        ::std::string base_name_{};

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
        explicit dut_context(::verilator_utils::dut_context_option option = {}) : coverage_{option.coverage}
        {
            // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer)
            auto&& current_test{*::doctest::getContextOptions()->currentTest};
            context_ = ::std::make_unique<::VerilatedContext>();
            context_->commandArgs(option.argc.value_or(::verilator_utils::detail::dut_context_default_args::args.size()),
                                  option.argv.value_or(::verilator_utils::detail::dut_context_default_args::args.data()));
            dut_ = ::std::make_unique<dut_t>(context_.get(),
                                             current_test.m_test_suite == nullptr ? "TOP" : current_test.m_test_suite);
            // 覆盖dut内的timescale设置
            context_->timeprecision(::std::to_underlying(option.time_precision));
            context_->timeunit(::std::to_underlying(option.time_unit));
            scheduler_ = ::std::make_unique<::verilator_utils::eval_scheduler>(*dut_);
            base_name_ = option.base_name.empty() ? current_test.m_name : option.base_name;

            if constexpr(use_tracer)
            {
                static_assert(dut_t::traceCapable, "Verilator生成代码时未开启trace支持");
                context_->traceEverOn(true);
                tracer_.init();
                dut_->trace(tracer_.get(), option.trace_level);
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
            dut_->final();
            if(coverage_ && scheduler_->eval_stage() != ::verilator_utils::eval_scheduler::eval_stage_enum::not_begin)
            {
                context_->coverageFilename(::std::format("{}.dat"sv, base_name_));
                context_->coveragep()->write();
            }
        }

        /**
         * @brief 执行一次调度器循环，如果启用波形记录器，则记录波形
         *
         */
        void loop_once()
        {
            scheduler_->loop_once();
            if constexpr(use_tracer) { tracer_.dump(context_->time()); }
        }

        /**
         * @brief 执行调度器的初始化循环，如果启用波形记录器，则记录波形
         *
         * @note 会创建波形记录文件
         */
        void initial_eval()
        {
            scheduler_->initial_eval();
            if constexpr(use_tracer)
            {
                tracer_.open(base_name_);
                tracer_.dump(context_->time());
            }
        }

        /**
         * @brief 获取当前上下文中生成文件的基本名称，不带后缀名
         *
         * @return 文件基本名称
         */
        [[nodiscard]] ::std::string_view base_name() const noexcept { return base_name_; }

        /**
         * @brief 设置生成文件的基本名称，不带后缀名
         *
         * @note 由于initial_eval会创建记录文件，因此必须在initial_eval前设置
         * @param base_name 文件基本名称
         */
        void set_base_name(::std::string_view base_name)
        {
            ::verilator_utils::check{}(scheduler_->eval_stage() == ::verilator_utils::eval_scheduler::eval_stage_enum::not_begin,
                                       "必须在initial_eval之前设置文件基本名称"sv);
            this->base_name_ = base_name;
        }

        /**
         * @brief 获取Verilator上下文对象引用
         *
         * @return Verilator上下文对象引用
         */
        auto&& context(this auto&& self) noexcept { return *self.context_; }

        /**
         * @brief 获取DUT对象引用
         *
         * @return DUT对象引用
         */
        auto&& dut(this auto&& self) noexcept { return *self.dut_; }

        /**
         * @brief 获取调度器对象引用
         *
         * @return 调度器对象引用
         */
        auto&& scheduler(this auto&& self) noexcept { return *self.scheduler_; }

        /**
         * @brief 获取跟踪器引用
         *
         * 只有当tracer_t不为void，即启用跟踪器时可调用
         * @return 跟踪器引用
         */
        auto&& tracer(this auto&& self) noexcept
            requires (use_tracer)
        { return *self.tracer_.get(); }

        /**
         * @brief 获取VerilatorContext的随机种子
         *
         * @return 随机种子
         */
        ::std::size_t seed() noexcept
        {
            if(!random_seed_logger.has_value())
            {
                random_seed_logger.emplace(::doctest::detail::MakeContextScope(log_random_seed{context_.get()}));
            }
            return static_cast<::std::size_t>(context_->randSeed());
        }

        /**
         * @brief 获取当前可执行文件所在路径
         *
         * @return 可执行文件所在路径
         */
        [[nodiscard]] ::std::filesystem::path binary_path() const
        { return ::std::filesystem::canonical(::verilator_utils::detail::dut_context_default_args::args[0]); }

        /**
         * @brief 判断当前上下文中覆盖率记录是否启用
         *
         * @return 覆盖率记录是否启用
         */
        [[nodiscard]] bool coverage() const noexcept { return coverage_; }

        /**
         * @brief 设置覆盖率记录是否启用
         *
         * @param enable_coverage 覆盖率记录是否启用
         */
        void set_coverage(bool enable_coverage) noexcept { coverage_ = enable_coverage; }

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
            while(!scheduler_->empty() && !scheduler_->is_finish()) { loop_once(); }
        }

        /**
         * @brief 向调度器中添加任务
         *
         * @param task 要添加的任务
         * @note 相当于在绑定的调度器对象scheduler上调用add_task
         */
        void add_task(::verilator_utils::task<void> task) { scheduler_->add_task(::std::move(task)); }

        /**
         * @brief 获取统计信息
         *
         * @return 统计信息
         */
        [[nodiscard]] ::verilator_utils::dut_context_stats stats() const
        {
            auto simtime{scheduler_->time_in_time_unit()};
            // 时间单位的指数，e.g. fs -> -15
            auto time_unit{context_->timeunit()};
            auto walltime{context_->statWallTimeSinceStart()};
            auto speed{simtime / walltime};
            ::std::uint64_t memory_peak{};
            ::std::uint64_t memory_current{};
            ::VlOs::memUsageBytes(memory_peak, memory_current);

            auto scaled_speed{speed};
            // 对齐到IEEE标准
            auto scaled_time_unit{(time_unit - 2) / 3 * 3};
            scaled_speed *= ::std::pow(10.0, time_unit - scaled_time_unit);
            // 对仿真速度进行缩放，最大为s，最小为fs
            // 边界判定必须排除端点单位，否则会越过time_unit_table的范围
            while(scaled_speed > 1e3 && scaled_time_unit < 0)
            {
                scaled_speed *= 1e-3;
                scaled_time_unit += 3;
            }
            while(scaled_speed < 1.0 && scaled_time_unit > -15)
            {
                scaled_speed *= 1e3;
                scaled_time_unit -= 3;
            }
            auto scaled_time_unit_suffix{
                ::std::get<2>(*::std::ranges::find_if(::verilator_utils::detail::time_unit_table, [&](const auto& item) {
                    return ::std::get<0>(item) == scaled_time_unit;
                }))};

            return {scheduler_->time_in_string(),
                    ::std::format("{:.3f}{}/s", scaled_speed, scaled_time_unit_suffix),
                    context_->threadsInModels(),
                    simtime,
                    walltime,
                    context_->statCpuTimeSinceStart(),
                    speed,
                    static_cast<double>(memory_peak) / 1024.0 / 1024.0};
        }
    };
}  // namespace verilator_utils

export namespace std
{
    template <>
    struct formatter<::verilator_utils::dut_context_stats>
    {
        constexpr static auto parse(::std::format_parse_context& ctx)
        {
            return ::verilator_utils::detail::parse_format_string_without_flags(
                ctx,
                "无效的verilator_utils::dut_context_stats格式符"sv);
        }

        template <typename iter_t>
        static auto format(const ::verilator_utils::dut_context_stats& value, ::std::basic_format_context<iter_t, char>& ctx)
        {
            return ::std::format_to(
                ctx.out(),
                "- 仿真器: {}\n- 仿真时间: {} 挂钟时间: {:.3f}s 仿真速度: {}\n- CPU时间: {:.3f}s 并发线程数: {} 内存峰值: {:.3f}MB"sv,
                value.stimulator,
                value.simtime_str,
                value.walltime,
                value.speed_str,
                value.cputime,
                value.threads,
                value.memory_peak);
        }
    };
}  // namespace std

export namespace doctest
{
    template <>
    struct StringMaker<::verilator_utils::dut_context_stats>
    {
        static ::doctest::String convert(const ::verilator_utils::dut_context_stats& value)
        { return ::std::format("\n{}"sv, value); }
    };
}  // namespace doctest
