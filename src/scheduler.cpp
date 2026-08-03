module;
#include <doctest_macros.hpp>
export module verilator_utils:scheduler;
import :wrapper;

export namespace verilator_utils
{
    /**
     * @brief 仿真结束异常类，用于实现协作式取消
     *
     * @note 可等待体需要在await_resume函数中检查评估是否结束，若结束则抛出该异常
     */
    struct eval_finish_exception
    {
    };

    /**
     * @brief 基于协程的电路评估调度器
     *
     */
    struct eval_scheduler;

    namespace detail
    {
        struct promise_base;
    }

    /**
     * @brief 判断类型promise_type是否为协程框架支持的承诺类型
     *
     * @tparam promise_type 要判断的类型
     */
    template <typename promise_type>
    concept is_coroutine_promise =
        ::std::derived_from<promise_type, ::verilator_utils::detail::promise_base> && ::std::is_final_v<promise_type>;

    namespace detail
    {
        /**
         * @brief 永不挂起的可等待体
         *
         */
        struct no_suspend_awaiter : ::std::suspend_never
        {
            void set_handle(this auto&& self, auto handle) noexcept(noexcept(self.set_handle_impl(handle)))
                requires (requires() {
                    { self.set_handle_impl(handle) } -> ::std::same_as<void>;
                })
            { self.set_handle_impl(handle); }
        };

        /**
         * @brief 协程状态枚举
         *
         */
        enum class status_enum : ::std::uint8_t
        {
            /// 协程正在创建
            creating,
            /// 初始化执行完毕
            initial_suspend,
            /// 协程正在执行，和是否被挂起无关
            running,
            /// 协程执行完毕
            finial_suspend,
        };

        /**
         * @brief 协程承诺类型的基类
         *
         */
        struct promise_base
        {
            /// 异常指针
            ::std::exception_ptr exception{};
            /// 父协程柄
            /// - 为nullptr表示没有父协程
            /// - 非nullptr表示该协程为子协程，生命周期由父协程管理
            ::std::coroutine_handle<> parent{};
            /// 类型擦除的父协程承诺
            promise_base* parent_promise{};
            /// 调度器指针，用于实现隐式的调度器传递
            ::verilator_utils::eval_scheduler* scheduler{};
            /// 任务是否是通过抛出仿真结束异常结束的
            bool is_eval_finish_exception{};
            /// 协程状态
            ::verilator_utils::detail::status_enum status{::verilator_utils::detail::status_enum::creating};
            /// 是否为异步协程
            /// - 为false表示同步协程，执行完毕后立即跳转到父协程
            /// - 为true表示异步协程
            bool is_async{};

            /// - 无父的同步协程为根协程，生命周期由调度器管理
            /// - 有父的同步协程为同步子协程，生命周期由父协程的task对象管理
            /// - 无父的异步协程为异步子协程，生命周期由父协程的async_task对象管理
            /// - 有父的异步协程为等待被join的异步子协程，生命周期由父协程的async_task对象管理

            /**
             * @brief 协程初始挂起
             *
             * @return 可等待体，总是挂起协程
             */
            auto initial_suspend() noexcept
            {
                status = status_enum::initial_suspend;

                struct initial_awaiter : ::std::suspend_always
                {
                    explicit initial_awaiter(status_enum& status) noexcept : ::std::suspend_always{}, status{status} {}

                    status_enum& status;

                    void await_resume() noexcept { status = status_enum::running; }
                };

                return initial_awaiter{status};
            }

            /**
             * @brief 协程最终挂起
             *
             * @return 挂起协程，若存在父协程则跳转到父协程执行，
             */
            auto final_suspend() noexcept;

            /**
             * @brief 判断该协程是不是由调度器直接管理的根协程
             *
             * @return 是否为根协程
             */
            [[nodiscard]] bool is_root_coroutine() const { return !is_async && parent == nullptr; }

            /**
             * @brief 检查任务是否绑定到调度器
             *
             * @return 已绑定则返回调度器指针，否则断言失败
             */
            [[nodiscard]] ::verilator_utils::eval_scheduler* check_scheduler() const
            {
                using namespace ::std::string_view_literals;
                REQUIRE_MESSAGE(scheduler != nullptr, "任务必须绑定调度器"sv);
                return scheduler;
            }

            /**
             * @brief 将协程中抛出的异常存储到异常指针中
             *
             */
            void unhandled_exception() noexcept
            {
                try
                {
                    throw;
                }
                catch(const ::verilator_utils::eval_finish_exception&)
                {
                    exception = ::std::current_exception();
                    is_eval_finish_exception = true;
                }
                catch(...)
                {
                    exception = ::std::current_exception();
                }
            }

            /**
             * @brief 获取承诺体中是否存在未处理异常
             *
             * @return 是否存在未处理异常
             */
            [[nodiscard]] bool with_unhandled_exception() const noexcept { return exception && !is_eval_finish_exception; }

            /**
             * @brief 重新抛出协程中抛出的异常
             *
             * @note 若协程是通过抛出仿真结束异常结束的，则不重新抛出异常
             */
            void rethrow_exception() const
            {
                if(with_unhandled_exception()) { ::std::rethrow_exception(exception); }
            }

            /**
             * @brief 判断协程是否已通过co_return退出
             *
             * @return 是否已退出
             */
            template <::verilator_utils::is_coroutine_promise promise_type>
            bool is_coroutine_returned(this promise_type& self) noexcept
            {
                auto handle{::std::coroutine_handle<promise_type>::from_promise(self)};
                return handle.done() && !self.exception;
            }

            /**
             * @brief 转发可等待体
             *
             * @tparam type 可等待体类型
             * @param awaiter 可等待体对象
             * @return auto&& 转发的可等待体对象
             */
            template <::verilator_utils::is_coroutine_promise promise_type, typename type>
            auto&& await_transform(this promise_type& self, type&& awaiter)
            {
                if constexpr(::std::derived_from<type, ::verilator_utils::detail::no_suspend_awaiter>)
                {
                    // 通过set_handle向可等待体传递协程柄
                    awaiter.set_handle(::std::coroutine_handle<promise_type>::from_promise(self));
                }
                return ::std::forward<type>(awaiter);
            }
        };

        /**
         * @brief 协程返回值实现
         *
         * @tparam return_t 返回类型
         */
        template <typename return_t>
        struct promise_with_return
        {
            using return_type = return_t;

            union buffer_t
            {
                return_type value;

                constexpr buffer_t() noexcept {}

                constexpr ~buffer_t() noexcept {}

                constexpr buffer_t(const buffer_t&) noexcept = delete;
                constexpr buffer_t(buffer_t&&) noexcept = delete;
                constexpr buffer_t& operator= (const buffer_t&) noexcept = delete;
                constexpr buffer_t& operator= (buffer_t&&) noexcept = delete;
            } buffer;

            /**
             * @brief 将返回值置于承诺体中
             *
             * @tparam value_type 返回值类型
             * @param value 返回值
             */
            template <typename value_type>
                requires (::std::constructible_from<return_type, value_type &&>)
            void return_value(value_type&& value) noexcept(::std::is_nothrow_constructible_v<return_type, value_type&&>)
            { ::std::construct_at(::std::addressof(buffer.value), ::std::forward<value_type>(value)); }

            /**
             * @brief 从承诺体中获取返回值
             *
             * @note 必须在调用过return_value后才能调用
             * @return 返回值
             */
            return_type get_return_value() noexcept(::std::is_nothrow_move_constructible_v<return_type>)
            { return ::std::move(buffer.value); }

            /**
             * @brief 析构承诺体中的返回值
             *
             * @note 必须在调用过return_value后才能调用
             */
            void destroy_return_value() noexcept { ::std::destroy_at(::std::addressof(buffer.value)); }
        };

        template <typename return_t>
            requires (::std::is_reference_v<return_t>)
        struct promise_with_return<return_t>
        {
            using return_type = return_t;
            using pointer = ::std::add_pointer_t<::std::remove_reference_t<return_type>>;

            pointer ptr{};

            /**
             * @brief 将返回值置于承诺体中
             *
             * @param ref 返回值
             */
            void return_value(auto&& ref) noexcept
                requires (::std::convertible_to<decltype(ref), return_type>)
            { ptr = ::std::addressof(ref); }

            /**
             * @brief 从承诺体中获取返回值
             *
             * @note 必须在调用过return_value后才能调用
             * @return 返回值
             */
            return_type get_return_value() noexcept { return *ptr; }

            /**
             * @brief 析构承诺体中的返回值
             *
             * @note 必须在调用过return_value后才能调用
             */
            void destroy_return_value() noexcept {}
        };

        /**
         * @brief 协程返回空值的特化
         *
         */
        template <>
        struct promise_with_return<void>
        {
            using return_type = void;

            /**
             * @brief 返回空值
             *
             */
            static void return_void() noexcept {}

            /**
             * @brief 从承诺体中获取返回值
             *
             * @note 必须在调用过return_value后才能调用
             */
            static void get_return_value() noexcept {}

            /**
             * @brief 析构承诺体中的返回值
             *
             * @note 必须在调用过return_value后才能调用
             */
            void destroy_return_value() noexcept {}
        };

        template <typename promise_type>
        struct subtask_awaiter
        {
            using handle_t = ::std::coroutine_handle<>;
            using return_type = promise_type::return_type;
            /// 子任务的协程句柄
            handle_t subhandle;
            promise_type& promise;

            /**
             * @brief 检查子任务是否完成
             *
             * @return 子任务是否完成
             */
            [[nodiscard]] bool await_ready() const noexcept { return subhandle.done(); }

            /**
             * @brief 挂起当前任务并跳转到子任务执行，等待子任务完成后恢复当前任务执行
             *
             * @param parent 当前任务的协程句柄
             * @return 子任务的协程句柄
             */
            [[nodiscard]] handle_t await_suspend(auto parent) const noexcept
            {
                promise.parent = parent;
                promise.parent_promise = ::std::addressof(parent.promise());
                promise.scheduler = parent.promise().scheduler;
                return subhandle;
            }

            /**
             * @brief 恢复当前任务执行
             *
             * @throws eval_finish_exception 若仿真已结束，抛出异常以实现协作式取消
             * @throws 若子任务抛出异常，则重新抛出异常
             */
            return_type await_resume();
        };
    }  // namespace detail

    /**
     * @brief 同步任务类型
     *
     */
    template <typename return_t = void>
    struct task
    {
        struct promise_type;
        /// 协程句柄类型
        using handle_t = ::std::coroutine_handle<promise_type>;
        /// 协程状态枚举
        using status_enum = ::verilator_utils::detail::status_enum;

        /**
         * @brief 同步任务的承诺类型
         *
         */
        struct promise_type final  // NOLINT(cppcoreguidelines-special-member-functions,misc-multiple-inheritance)
            :
            ::verilator_utils::detail::promise_base,
            ::verilator_utils::detail::promise_with_return<return_t>
        {
        private:
            using base_t = ::verilator_utils::detail::promise_with_return<return_t>;
            using base_t::destroy_return_value;
            using base_t::get_return_value;

        public:
            using typename base_t::return_type;

            /**
             * @brief 析构协程帧内储存的返回值
             *
             */
            ~promise_type() noexcept
            {
                if(is_coroutine_returned()) { destroy_return_value(); }
            }

            /**
             * @brief 获取任务的返回对象
             *
             * @return 任务对象
             */
            task get_return_object() noexcept { return task{handle_t::from_promise(*this)}; }

            /**
             * @brief 从承诺中获取协程结果
             *
             * @note 使用移动构造将结果所有权转移到外部
             */
            [[nodiscard]] return_type get_result()
            {
                REQUIRE(is_coroutine_returned());
                return get_return_value();
            }
        };

        /**
         * @brief 任务构造函数
         *
         * @param handle 协程句柄
         */
        explicit task(handle_t handle) noexcept : handle{handle} {}

        /**
         * @brief 任务析构函数，销毁协程句柄
         *
         */
        ~task() noexcept { destroy(); }

        task(const task& other) noexcept = delete;
        task& operator= (const task& other) noexcept = delete;
        task& operator= (task&& other) noexcept = delete;

        task(task&& other) noexcept : handle{::std::exchange(other.handle, nullptr)} {}

        /**
         * @brief 检查任务对象是否绑定了协程柄
         *
         * @return 是否绑定了协程柄
         */
        explicit operator bool() const noexcept { return static_cast<bool>(handle); }

        /**
         * @brief 判断任务对象是否可同步
         *
         * @return 是否可同步
         */
        [[nodiscard]] bool joinable() const noexcept { return static_cast<bool>(handle); }

        /**
         * @brief 检查任务是否完成
         *
         * @return 任务是否完成
         */
        [[nodiscard]] bool done() const noexcept { return handle.done(); }

        /**
         * @brief 恢复任务执行
         *
         */
        void resume() noexcept { handle.resume(); }

        /**
         * @brief 重新抛出任务中抛出的异常
         *
         * @note 若任务是通过抛出仿真结束异常结束的，则不重新抛出异常
         */
        void rethrow_exception() const { handle.promise().rethrow_exception(); }

        /**
         * @brief 分离任务的协程句柄，此后任务不再持有该句柄
         *
         * @return 任务的协程句柄
         */
        [[nodiscard]] handle_t detach() noexcept { return ::std::exchange(handle, nullptr); }

        /**
         * @brief 获取任务的协程句柄
         *
         * @return 任务的协程句柄
         */
        [[nodiscard]] handle_t get_handle() const noexcept { return handle; }

        /**
         * @brief 获取任务的promise对象
         *
         * @return 任务的promise对象引用
         */
        [[nodiscard]] promise_type& get_promise() const noexcept { return handle.promise(); }

        /**
         * @brief 销毁任务的协程句柄
         *
         */
        void destroy() noexcept
        {
            if(handle) { ::std::exchange(handle, nullptr).destroy(); }
        }

        /**
         * @brief 调用子任务，立即跳转到子任务执行，等待子任务完成后恢复当前任务执行
         *
         * @return 可等待体
         */
        friend ::verilator_utils::detail::subtask_awaiter<promise_type> operator co_await(const task& subtask)
        {
            REQUIRE(subtask.joinable());
            return {subtask.handle, subtask.get_promise()};
        }

    private:
        handle_t handle;
    };

    /**
     * @brief 检查类型是否为事件回调函数，即返回bool的可调用类型
     *
     * @note 回调函数返回true表示事件发生
     * @tparam type 要检查的类型
     */
    template <typename type>
    concept is_event_callback = ::std::is_invocable_r_v<bool, type>;

    /// 默认事件生成器类型
    using default_event_callback = ::std::function<bool()>;

    /**
     * @brief 边沿检测器
     *
     */
    struct edge_detector
    {
    public:
        /**
         * @brief 要检测的边沿类型
         *
         */
        enum class edge_enum : ::std::uint8_t
        {
            /// 上升沿
            rising = 1,
            /// 下降沿
            falling = 2,
            /// 双边沿
            both = rising | falling,
        };

        using enum edge_enum;

        /**
         * @brief 构造边沿检测器对象
         *
         * @tparam callback_t 事件回调类型
         * @param event_callback 事件回调函数
         * @param edge_to_detect 要检测的边沿
         */
        edge_detector(const ::verilator_utils::is_bit_slice auto& bit, edge_enum edge_to_detect) :
            callback{[bit] { return static_cast<bool>(bit); }}, previous_value{static_cast<bool>(bit)},
            edge_to_detect{edge_to_detect}
        {
        }

        /**
         * @brief 获取边沿检测结果
         *
         * @return 是否出现要检测的边沿
         */
        bool operator() ()
        {
            bool current_value{callback()};
            bool previous_value{::std::exchange(this->previous_value, current_value)};
            switch(edge_to_detect)
            {
                case rising: return !previous_value && current_value;
                case falling: return previous_value && !current_value;
                case both: return previous_value != current_value;
                default: ::std::unreachable();
            }
        }

        /**
         * @brief 获取要检测的边沿类型
         *
         * @return 要检测的边沿类型
         */
        [[nodiscard]] edge_enum get_edge_to_detect() const { return edge_to_detect; }

        /**
         * @brief 设置要检测的边沿类型
         *
         * @param new_edge_to_detect 要检测的边沿类型
         * @return 先前设置的边沿类型
         */
        edge_enum set_edge_to_detect(edge_enum new_edge_to_detect) { return ::std::exchange(edge_to_detect, new_edge_to_detect); }

    private:
        ::verilator_utils::default_event_callback callback;
        bool previous_value;
        edge_enum edge_to_detect;
    };
}  // namespace verilator_utils

export namespace verilator_utils::detail
{
    /**
     * @brief 协程状态对，包含协程柄和类型擦除的承诺指针
     *
     */
    struct coroutine_pair
    {
        /// 协程柄
        ::std::coroutine_handle<> handle;
        /// 类型擦除的承诺指针
        ::verilator_utils::detail::promise_base* promise;

        // NOLINTBEGIN(*-explicit-constructor)

        /**
         * @brief 从未类型擦除的协程柄构造状态对
         *
         * @tparam promise_type 承诺类型
         * @param handle 未类型擦除的协程柄
         */
        template <::verilator_utils::is_coroutine_promise promise_type>
        coroutine_pair(::std::coroutine_handle<promise_type> handle) noexcept :
            handle{handle}, promise{::std::addressof(handle.promise())}
        {
        }

        /**
         * @brief 从子协程承诺中保存的父协程状态构造状态对
         *
         * @param subtask_promise 子协程承诺
         */
        coroutine_pair(const ::verilator_utils::detail::promise_base& subtask_promise) noexcept :
            handle{subtask_promise.parent}, promise{subtask_promise.parent_promise}
        {
        }

        // NOLINTEND(*-explicit-constructor)
    };

    /**
     * @brief 等待队列的元素类型
     *
     */
    struct wait_queue_element
    {
        /// 等待时间
        ::std::uint64_t target_time;
        /// 协程状态对
        ::verilator_utils::detail::coroutine_pair pair;

        /// 等待队列元素的比较运算符，按等待时间点进行比较
        friend ::std::strong_ordering operator<=> (const wait_queue_element& self, const wait_queue_element& other) noexcept
        { return self.target_time <=> other.target_time; }
    };

    /// 等待队列类型
    using wait_queue_t = ::std::priority_queue<::verilator_utils::detail::wait_queue_element,
                                               ::std::vector<::verilator_utils::detail::wait_queue_element>,
                                               ::std::greater<>>;

    /**
     * @brief 事件队列的元素类型
     *
     */
    struct event_queue_element
    {
        /// 事件回调函数，判断事件是否完成
        ::verilator_utils::default_event_callback* event_callback;
        /// 协程状态对
        ::verilator_utils::detail::coroutine_pair pair;

        [[nodiscard]] bool is_ready() const
        {
            REQUIRE_NE(event_callback, nullptr);
            return (*event_callback)();
        }
    };

    /// 事件队列类型
    using event_queue_t = ::std::vector<::verilator_utils::detail::event_queue_element>;

    /// 就绪队列类型
    using ready_queue_t = ::std::vector<::verilator_utils::detail::coroutine_pair>;
}  // namespace verilator_utils::detail

export namespace verilator_utils
{
    struct eval_scheduler
    {
        /**
         * @brief 评估阶段枚举
         *
         */
        enum class eval_stage_enum : ::std::size_t
        {
            // 未注明的阶段可进行等待
            // --- 初始化阶段 ---

            /// 尚未开始评估
            not_begin,
            /// 初始评估后，该阶段不进行协程调度
            after_initial_eval,

            // --- 仿真循环阶段 ---

            /// 评估已就绪任务
            eval_ready_task,
            /// 电路评估前
            before_dut_eval,
            /// 评估电路中，该阶段不进行协程调度
            on_dut_eval,
            /// 电路评估后
            after_dut_eval,
            /// 一轮评估完成，该阶段不进行协程调度，不可等待
            eval_end,

            /// 非法状态，可用于默认参数等场合
            invalid = -1zu
        };

    private:
        /// 指向VerilatedModel的指针，由dut类型擦除得到
        ::VerilatedModel* dut;
        /// dut状态计算函数指针类型
        using dut_eval_t = void (*)(::VerilatedModel*);
        /// dut状态计算函数
        dut_eval_t dut_eval;
        /// 时间精度，单位为飞秒
        ::std::size_t time_precision_fs;
        /// 每dut时间单位对应的dut时间精度
        double time_precision_per_time_unit;
        /// 每dut时间精度对应的格式化输出单位
        double output_unit_per_time_precision;
        /// 格式化输出的时间单位后缀
        ::std::string_view output_unit_suffix;

        /// 等待队列
        ::verilator_utils::detail::wait_queue_t wait_queue{};
        /// 事件队列
        ::verilator_utils::detail::event_queue_t event_queue{};
        /// 就绪队列
        ::verilator_utils::detail::ready_queue_t ready_queue{};

        /// 评估阶段
        eval_stage_enum eval_stage{eval_stage_enum::not_begin};

        /**
         * @brief 恢复协程执行，若协程为根协程且执行完则销毁协程
         *
         * @param handle 协程柄
         */
        static void resume_coroutine(::verilator_utils::detail::coroutine_pair pair)
        {
            auto [handle, promise]{pair};
            auto is_root{promise->is_root_coroutine()};
            handle.resume();
            // 协程为根协程时执行销毁和异常传播
            if(is_root && handle.done())
            {
                // 利用raii确保在异常时销毁handle
                constexpr static auto deleter{[](::std::coroutine_handle<>* handle) static noexcept { handle->destroy(); }};
                std::unique_ptr<::std::coroutine_handle<>, decltype(deleter)> _{&handle};
                promise->rethrow_exception();
            }
        }

        /**
         * @brief 评估等待队列，推进时间步，将就绪协程放入就绪队列
         *
         */
        void wait_queue_eval()
        {
            if(!wait_queue.empty())
            {
                auto target_time{wait_queue.top().target_time};
                // 推进时间步
                dut->contextp()->time(target_time);
                // 将就绪协程放入就绪队列
                while(!wait_queue.empty())
                {
                    if(auto&& [task_target_time, pair]{wait_queue.top()}; task_target_time == target_time)
                    {
                        ready_queue.emplace_back(pair);
                        wait_queue.pop();
                    }
                    else
                    {
                        break;
                    }
                }
            }
        }

        /**
         * @brief 评估事件队列，将就绪协程放入就绪队列
         *
         * @return 是否有协程就绪
         */
        bool event_queue_eval()
        {
            bool any_coroutine_ready{};
            for(auto index{0zu}; index != event_queue.size();)
            {
                if(auto&& ref{event_queue[index]}; ref.is_ready())
                {
                    ready_queue.emplace_back(ref.pair);
                    ref = event_queue.back();
                    event_queue.pop_back();
                    any_coroutine_ready = true;
                }
                else
                {
                    ++index;
                }
            }

            return any_coroutine_ready;
        }

        /**
         * @brief 评估就绪队列
         *
         */
        bool ready_queue_eval()
        {
            bool any_coroutine_run{!ready_queue.empty()};
            auto i{0zu};
            try
            {
                for(; i != ready_queue.size(); ++i) { resume_coroutine(ready_queue[i]); }
            }
            catch(...)
            {
                auto begin{ready_queue.begin()};
                ready_queue.erase(begin, begin + static_cast<::std::ptrdiff_t>(i) + 1);
                throw;
            }
            ready_queue.clear();
            return any_coroutine_run;
        }

    public:
        /**
         * @brief 构造调度器对象
         *
         * @tparam dut_t 待测模型类型，必须派生自VerilatedModel
         * @param dut 指向待测模型对象的指针
         * @note 调度器会缓存time precision和time unit，因此在构造时需要确保二者已经设置
         */
        template <::std::derived_from<::VerilatedModel> dut_t>
        explicit eval_scheduler(dut_t& dut) noexcept
        {
            // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer)
            this->dut = &dut;
            dut_eval = [](::VerilatedModel* dut) { static_cast<dut_t*>(dut)->eval(); };
            auto&& context{*dut.contextp()};
            auto time_precision{context.timeprecision()};
            auto time_unit{context.timeunit()};
            time_precision_fs = static_cast<::std::uint64_t>(::std::pow(10, 15 + time_precision));
            time_precision_per_time_unit = static_cast<::std::uint64_t>(::std::pow(10, time_unit - time_precision));
            using namespace ::std::string_view_literals;
            constexpr static ::std::array unit_table{
                ::std::tuple{0,   1'000'000'000'000'000zu, "s"sv },
                ::std::tuple{-3,  1'000'000'000'000zu,     "ms"sv},
                ::std::tuple{-6,  1'000'000'000zu,         "us"sv},
                ::std::tuple{-9,  1'000'000zu,             "ns"sv},
                ::std::tuple{-12, 1'000zu,                 "ps"sv},
                ::std::tuple{-15, 1zu,                     "fs"sv},
            };
            for(auto&& [unit_exponent, unit_fs, unit_suffix]: unit_table)
            {
                if(time_unit >= unit_exponent)
                {
                    output_unit_per_time_precision = static_cast<double>(time_precision_fs) / static_cast<double>(unit_fs);
                    output_unit_suffix = unit_suffix;
                    return;
                }
            }
            ::std::unreachable();
            // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
        }

        eval_scheduler(const eval_scheduler&) = delete;
        eval_scheduler& operator= (const eval_scheduler&) = delete;
        eval_scheduler(eval_scheduler&&) noexcept = default;
        eval_scheduler& operator= (eval_scheduler&&) noexcept = default;

        /**
         * @brief 检查调度器是否为空
         *
         * @return 调度器是否为空
         */
        [[nodiscard]] bool empty() const noexcept { return wait_queue.empty() && event_queue.empty() && ready_queue.empty(); }

        /**
         * @brief 检查仿真是否结束
         *
         * @return 仿真是否结束
         */
        [[nodiscard]] bool is_finish() const noexcept { return dut->contextp()->gotFinish(); }

        /**
         * @brief 标记仿真结束
         *
         */
        void finish() noexcept { dut->contextp()->gotFinish(true); }

        /**
         * @brief 仿真结束时抛出eval_finish_exception异常
         *
         * @throw eval_finish_exception 若仿真已结束，抛出异常以实现协作式取消
         */
        void throw_if_finish() const
        {
            if(is_finish()) { throw ::verilator_utils::eval_finish_exception{}; }
        }

        /**
         * @brief 获取待测模型对象的引用
         *
         * @tparam dut_t 待测模型类型，必须派生自VerilatedModel
         * @return 待测模型对象的引用
         */
        template <::std::derived_from<::VerilatedModel> dut_t = ::VerilatedModel>
        dut_t& get_dut() const noexcept
        { return *static_cast<dut_t*>(dut); }

        /**
         * @brief 获取当前时间，单位为dut时间精度
         *
         * @return 当前时间
         */
        [[nodiscard]] ::std::uint64_t time_in_time_precision() const noexcept { return dut->contextp()->time(); }

        /**
         * @brief 获取当前时间，单位为dut时间单位
         *
         * @return 当前时间
         */
        [[nodiscard]] double time_in_time_unit() const noexcept
        { return static_cast<double>(time_in_time_precision()) / time_precision_per_time_unit; }

        /**
         * @brief 获取dut时间精度，单位为飞秒
         *
         * @return dut时间精度
         */
        [[nodiscard]] ::std::uint64_t get_time_precision_fs() const noexcept { return time_precision_fs; }

        /**
         * @brief 获取当前时间，已根据时间单位转换为字符串格式并添加时间单位后缀
         *
         * @return 当前时间的字符串表示
         */
        [[nodiscard]] ::std::string time_in_string() const
        {
            auto time_in_output_unit{static_cast<double>(time_in_time_precision()) * output_unit_per_time_precision};
            return ::std::format("{:.6g}{}", time_in_output_unit, output_unit_suffix);
        }

        /**
         * @brief 析构调度器对象
         *
         */
        ~eval_scheduler() noexcept
        {
            constexpr static auto do_destroy{
                [](eval_scheduler& scheduler, const ::verilator_utils::detail::coroutine_pair& pair) static noexcept
                {
                    auto [handle, promise]{pair};
                    if(promise->is_root_coroutine())
                    {
                        // 根协程直接销毁
                        handle.destroy();
                    }
                    else if(!promise->is_async)
                    {
                        // 同步非根协程的父协程不在调度队列中
                        // 将其父协程放入队列，由调度器进行销毁
                        try
                        {
                            scheduler.ready_queue.emplace_back(*promise);
                        }
                        catch(...)
                        {
                            ::std::terminate();
                        }
                        //  销毁子协程本身
                        handle.destroy();
                    }
                    // 异步非根协程的父协程在调度队列中
                    // 在处理其父协程时由父协程进行销毁
                    // 此处无需进行唤醒或销毁
                },
            };
            while(!wait_queue.empty())
            {
                do_destroy(*this, wait_queue.top().pair);
                wait_queue.pop();
            }

            for(auto&& [_, pair]: event_queue) { do_destroy(*this, pair); }
            event_queue.clear();

            for(auto i{0zu}; i != ready_queue.size(); ++i) { do_destroy(*this, ready_queue[i]); }
            ready_queue.clear();
        }

        /**
         * @brief 获取调度器当前评估阶段
         *
         * @return eval_stage_enum 评估阶段枚举
         */
        [[nodiscard]] eval_stage_enum get_eval_stage() const noexcept { return eval_stage; }

        /**
         * @brief 执行一轮评估
         *
         */
        void loop_once()
        {
            using enum eval_stage_enum;

            eval_stage = eval_ready_task;
            // 执行已就绪协程
            while(ready_queue_eval()) {}

            // 推进时间步，执行新的就绪协程
            wait_queue_eval();
            eval_stage = before_dut_eval;
            while(ready_queue_eval()) {}
            // 循环评估事件队列和就绪队列，直到收敛
            while(event_queue_eval()) { ready_queue_eval(); }

            // 评估电路，该步骤只评估verilator模型，不进行协程调度
            eval_stage = on_dut_eval;
            dut_eval(dut);

            // 循环评估事件队列和就绪队列，直到收敛
            eval_stage = after_dut_eval;
            while(event_queue_eval()) { ready_queue_eval(); }

            // 结束一轮评估
            eval_stage = eval_end;
        }

        /**
         * @brief 循环直到调度器为空或仿真结束
         *
         */
        void loop_until_finish()
        {
            while(!empty() && !is_finish()) { loop_once(); }
        }

        /**
         * @brief 循环直到就绪队列为空，用于完成信号初始化
         *
         * @note 用于在仿真开始时给信号设置初始值，只应该执行一次
         */
        void initial_eval()
        {
            using namespace ::std::string_view_literals;
            REQUIRE_MESSAGE(eval_stage <= eval_stage_enum::after_initial_eval, "已进入仿真循环阶段，不能执行初始化"sv);
            REQUIRE_MESSAGE(eval_stage == eval_stage_enum::not_begin, "已执行过initial_eval，不应再次执行"sv);
            while(ready_queue_eval()) {}
            eval_stage = eval_stage_enum::after_initial_eval;
        }

        /**
         * @brief 向事件队列中注册一个事件
         *
         * @param callback 事件回调函数
         * @param pair 协程状态对
         */
        void register_event(::verilator_utils::default_event_callback& callback, ::verilator_utils::detail::coroutine_pair pair)
        { event_queue.emplace_back(&callback, pair); }

        /**
         * @brief 向等待队列中注册一个等待时间
         *
         * @note 不支持delta延迟，等待时间不能为0
         * @param time_to_wait 等待时间，单位为飞秒，不能为0
         * @param pair 协程状态对
         */
        void register_wait(::verilator_utils::femtosecond_t time_to_wait, ::verilator_utils::detail::coroutine_pair pair)
        {
            using namespace ::std::string_view_literals;
            REQUIRE_MESSAGE(time_to_wait != 0_fs, "不支持delta延迟，等待时间不能为0"sv);
            auto time_to_wait_in_time_precision{time_to_wait.rep / time_precision_fs};
            REQUIRE_MESSAGE(time_to_wait_in_time_precision != 0, "等待时长小于时间精度，被截断为0"sv);
            wait_queue.emplace(time_to_wait_in_time_precision + dut->contextp()->time(), pair);
        }

        /**
         * @brief 向就绪队列中注册一个协程
         *
         * @param pair 协程状态对
         */
        void register_ready(::verilator_utils::detail::coroutine_pair pair) noexcept
        {
            try
            {
                ready_queue.emplace_back(pair);
            }
            catch(...)
            {
                ::std::terminate();
            }
        }

        /**
         * @brief 向调度器中添加任务
         *
         * @param task 要添加的任务
         */
        void add_task(::verilator_utils::task<void> task) noexcept
        {
            // 向task中添加调度器
            task.get_promise().scheduler = this;
            register_ready(task.detach());
        }
    };

    auto verilator_utils::detail::promise_base::final_suspend() noexcept
    {
        status = status_enum::finial_suspend;

        struct finial_awaiter
        {
            promise_base* promise;

            static bool await_ready() noexcept { return false; }

            [[nodiscard]] ::std::coroutine_handle<> await_suspend(::std::coroutine_handle<> /* unused */) const noexcept
            {
                // 无父协程则不进行回溯
                if(promise->parent == nullptr) { return ::std::noop_coroutine(); }
                else
                {
                    if(promise->parent_promise->is_root_coroutine())
                    {
                        // 父协程为根协程时需要调度器进行异常传播
                        // 因此将父协程放入调度器就绪队列
                        promise->scheduler->register_ready(*promise);
                        return ::std::noop_coroutine();
                    }
                    else
                    {
                        // 父协程为非根协程直接回溯
                        return promise->parent;
                    }
                }
            }

            static void await_resume() noexcept {}
        };

        return finial_awaiter{this};
    }

    template <typename promise_type>
    auto ::verilator_utils::detail::subtask_awaiter<promise_type>::await_resume() -> return_type
    {
        REQUIRE(subhandle.done());
        promise.scheduler->throw_if_finish();
        promise.rethrow_exception();
        return promise.get_result();
    }
}  // namespace verilator_utils
