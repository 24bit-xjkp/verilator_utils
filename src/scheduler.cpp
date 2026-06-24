module;
#include <doctest.h>
#include <verilated.h>
export module verilator_utils:scheduler;
import :wrapper;

export namespace verilator_utils
{
    /**
     * @brief 仿真结束异常类，用于实现协作式取消
     *
     * @note 等待体需要在await_resume函数中检查评估是否结束，若结束则抛出该异常
     */
    struct eval_finish_exception
    {
    };

    /**
     * @brief 同步任务类型
     *
     */
    struct task
    {
        struct promise_type;
        /// 协程句柄类型
        using handle_t = ::std::coroutine_handle<promise_type>;

        /**
         * @brief 在协程内获取协程柄，不会挂起协程
         *
         * @code {.cpp}
         * task foo()
         * {
         *     auto handle{co_wait task::get_handleTt{}};
         * }
         * @endcode
         */
        struct get_handle_t
        {
        };

        /**
         * @brief 任务状态枚举
         *
         */
        enum class status_enum : ::std::uint8_t
        {
            /// 任务正在创建
            creating,
            /// 初始化执行完毕
            initial_suspend,
            /// 任务正在执行，和是否被挂起无关
            running,
            /// 任务执行完毕
            finial_suspend
        };

        /**
         * @brief 同步任务的承诺类型
         *
         */
        struct promise_type
        {
            /// 异常指针
            ::std::exception_ptr exception{};
            /// 父协程句柄
            /// - 为空表示没有父协程，直接由调度器管理
            /// - 为std::noop_coroutine()表示由父协程管理，但执行完后不跳转到父协程
            /// - 为普通协程柄表示由父协程管理，执行完后跳转到父协程执行
            ::std::coroutine_handle<> parent{};
            /// 任务是否是通过抛出仿真结束异常结束的
            bool is_eval_finish_exception{};
            /// 任务状态
            status_enum status{status_enum::creating};

            /**
             * @brief 获取任务的返回对象
             *
             * @return 任务对象
             */
            inline task get_return_object() noexcept { return task{handle_t::from_promise(*this)}; }

            /**
             * @brief 任务初始挂起
             *
             * @return 可等待体，总是挂起任务
             */
            inline auto initial_suspend() noexcept
            {
                status = status_enum::initial_suspend;

                struct initial_awaiter : ::std::suspend_always
                {
                    inline initial_awaiter(status_enum& status) noexcept : ::std::suspend_always{}, status{status} {}

                    status_enum& status;

                    inline void await_resume() noexcept { status = status_enum::running; }
                };

                return initial_awaiter{status};
            }

            /**
             * @brief 任务最终挂起
             *
             * @return 挂起任务，若存在父协程则跳转到父协程执行
             */
            inline auto final_suspend() noexcept
            {
                struct finial_awaiter
                {
                    ::std::coroutine_handle<> parent{};

                    inline bool await_ready() const noexcept { return false; }

                    inline ::std::coroutine_handle<> await_suspend(::std::coroutine_handle<>) const noexcept
                    {
                        // 存在父协程则恢复父协程，否则无操作
                        return parent ? parent : ::std::noop_coroutine();
                    }

                    inline void await_resume() const noexcept {}
                };

                status = status_enum::finial_suspend;
                return finial_awaiter{parent};
            }

            /**
             * @brief 任务返回空值
             *
             */
            inline static void return_void() noexcept {}

            /**
             * @brief 将任务中抛出的异常存储到异常指针中
             *
             */
            inline void unhandled_exception() noexcept
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
            inline bool with_unhandled_exception() const noexcept { return exception && !is_eval_finish_exception; }

            /**
             * @brief 重新抛出任务中抛出的异常
             *
             * @note 若任务是通过抛出仿真结束异常结束的，则不重新抛出异常
             */
            inline void rethrow_exception() const
            {
                if(with_unhandled_exception()) { ::std::rethrow_exception(exception); }
            }

            /**
             * @brief 判断该协程是不是由调度器直接管理的根协程
             *
             * @return 是否为根协程
             */
            inline bool is_root_coroutine() const { return parent == nullptr; }

            /**
             * @brief 实现无挂起的协程柄获取
             *
             * @return 可等待体
             */
            inline auto await_transform(::verilator_utils::task::get_handle_t) noexcept
            {
                struct get_handle_awaiter
                {
                    handle_t handle;

                    /**
                     * @brief 判断是否立即就绪
                     *
                     * @return true 立即就绪以避免挂起协程
                     */
                    inline static bool await_ready() noexcept { return true; }

                    /**
                     * @brief 挂起协程
                     *
                     */
                    inline void await_suspend(handle_t) noexcept {}

                    /**
                     * @brief 恢复任务执行
                     *
                     * @return handle_t 当前任务的协程柄
                     */
                    inline handle_t await_resume() const noexcept { return handle; }
                };

                return get_handle_awaiter{handle_t::from_promise(*this)};
            }

            /**
             * @brief 转发可等待体
             *
             * @tparam type 可等待体类型
             * @param awaiter 可等待体对象
             * @return auto&& 转发的可等待体对象
             */
            template <typename type>
            inline auto&& await_transform(type&& awaiter) noexcept
            { return ::std::forward<type>(awaiter); }
        };

        /**
         * @brief 任务构造函数
         *
         * @param handle 协程句柄
         */
        inline task(handle_t handle) noexcept : handle{handle} {}

        /**
         * @brief 任务析构函数，销毁协程句柄
         *
         */
        inline ~task() noexcept
        {
            if(handle) { handle.destroy(); }
        }

        inline task(const task& other) noexcept = delete;
        inline task& operator= (const task& other) noexcept = delete;

        inline task(task&& other) noexcept : handle{::std::exchange(other.handle, nullptr)} {}

        inline task& operator= (task&& other) noexcept
        {
            if(handle) { handle.destroy(); }
            handle = ::std::exchange(other.handle, nullptr);
            return *this;
        }

        /**
         * @brief 检查任务对象是否绑定了协程柄
         *
         * @return 是否绑定了协程柄
         */
        inline explicit operator bool() const noexcept { return static_cast<bool>(handle); }

        /**
         * @brief 检查任务是否完成
         *
         * @note 未绑定协程柄也视为完成
         * @return 任务是否完成
         */
        inline bool done() const noexcept { return !handle || handle.done(); }

        /**
         * @brief 恢复任务执行
         *
         */
        inline void resume() noexcept { handle.resume(); }

        /**
         * @brief 重新抛出任务中抛出的异常
         *
         * @note 若任务是通过抛出仿真结束异常结束的，则不重新抛出异常
         */
        inline void rethrow_exception() const { handle.promise().rethrow_exception(); }

        /**
         * @brief 分离任务的协程句柄，此后任务不再持有该句柄
         *
         * @return 任务的协程句柄
         */
        inline handle_t detach() noexcept { return ::std::exchange(handle, handle_t{}); }

        /**
         * @brief 获取任务的协程句柄
         *
         * @return 任务的协程句柄
         */
        inline handle_t get_handle() const noexcept { return handle; }

        /**
         * @brief 获取任务的promise对象
         *
         * @return 任务的promise对象引用
         */
        inline task::promise_type& get_promise() const noexcept { return handle.promise(); }

        /**
         * @brief 销毁任务的协程句柄
         *
         */
        inline void destroy() noexcept
        {
            if(handle) { ::std::exchange(handle, nullptr).destroy(); }
        }

        /**
         * @brief 实现子任务的可等待体
         *
         */
        struct task_awaiter
        {
            /// 子任务的协程句柄
            handle_t subhandle;

            /**
             * @brief 检查子任务是否完成
             *
             * @return 子任务是否完成
             */
            inline bool await_ready() const noexcept { return !subhandle || subhandle.done(); }

            /**
             * @brief 挂起当前任务并跳转到子任务执行，等待子任务完成后恢复当前任务执行
             *
             * @param parent 当前任务的协程句柄
             * @return 子任务的协程句柄
             */
            inline handle_t await_suspend(handle_t parent) const noexcept
            {
                subhandle.promise().parent = parent;
                return subhandle;
            }

            /**
             * @brief 恢复当前任务执行
             *
             * @note 由于子任务结束后直接跳转到父任务执行，中间没有暂停点，因此不检查仿真是否结束
             * @throws 若子任务抛出异常，则重新抛出异常
             */
            inline void await_resume() const { subhandle.promise().rethrow_exception(); }
        };

        /**
         * @brief 调用子任务，立即跳转到子任务执行，等待子任务完成后恢复当前任务执行
         *
         * @return 可等待体
         */
        inline friend task_awaiter operator co_await(const task& subtask) noexcept { return task_awaiter{subtask.handle}; }

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
    private:
        ::verilator_utils::default_event_callback callback;
        bool previous_value;

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
            both = rising | falling
        } edge_to_detect;

    public:
        using enum edge_enum;

        /**
         * @brief 构造边沿检测器对象
         *
         * @tparam callback_t 事件回调类型
         * @param event_callback 事件回调函数
         * @param edge_to_detect 要检测的边沿
         */
        template <::verilator_utils::is_event_callback callback_t>
        inline edge_detector(callback_t&& event_callback, edge_enum edge_to_detect) :
            callback{::std::forward<callback_t>(event_callback)}, previous_value{event_callback()}, edge_to_detect{edge_to_detect}
        {
        }

        /**
         * @brief 获取边沿检测结果
         *
         * @return 是否出现要检测的边沿
         */
        inline bool operator() ()
        {
            bool current_value{callback()};
            bool previous_value{::std::exchange(this->previous_value, current_value)};
            switch(edge_to_detect)
            {
                case rising: return !previous_value && current_value;
                case falling: return previous_value && !current_value;
                case both: return previous_value != current_value;
            }
        }

        /**
         * @brief 获取要检测的边沿类型
         *
         * @return 要检测的边沿类型
         */
        inline edge_enum get_edge_to_detect() const { return edge_to_detect; }

        /**
         * @brief 设置要检测的边沿类型
         *
         * @param new_edge_to_detect 要检测的边沿类型
         * @return 先前设置的边沿类型
         */
        inline edge_enum set_edge_to_detect(edge_enum new_edge_to_detect)
        { return ::std::exchange(edge_to_detect, new_edge_to_detect); }
    };
}  // namespace verilator_utils

export namespace verilator_utils::detail
{
    /**
     * @brief 等待队列的元素类型
     *
     */
    struct wait_queue_element
    {
        /// 等待时间
        ::std::uint64_t target_time;
        /// 协程柄
        ::std::coroutine_handle<::verilator_utils::task::promise_type> handle;

        /// 等待队列元素的比较运算符，按等待时间点进行比较
        inline friend ::std::strong_ordering operator<=> (const wait_queue_element& self,
                                                          const wait_queue_element& other) noexcept
        { return self.target_time <=> other.target_time; }
    };

    /// 等待队列类型
    using wait_queue_t = ::std::priority_queue<::verilator_utils::detail::wait_queue_element,
                                               ::std::vector<::verilator_utils::detail::wait_queue_element>,
                                               ::std::greater<::verilator_utils::detail::wait_queue_element>>;

    /**
     * @brief 事件队列的元素类型
     *
     */
    struct event_queue_element
    {
        /// 事件回调函数，判断事件是否完成
        ::verilator_utils::default_event_callback* event_callback;
        /// 协程柄
        ::std::coroutine_handle<::verilator_utils::task::promise_type> handle;

        inline bool is_ready() const
        {
            REQUIRE_NE(event_callback, nullptr);
            return (*event_callback)();
        }
    };

    /// 事件队列类型
    using event_queue_t = ::std::vector<::verilator_utils::detail::event_queue_element>;

    /// 就绪队列类型
    using ready_queue_t = ::std::stack<::std::coroutine_handle<::verilator_utils::task::promise_type>>;

    /**
     * @brief 判断是否是task类型的引用
     *
     * @tparam type 要判断的类型
     */
    template <typename type>
    concept is_task_reference = ::std::same_as<::std::remove_reference_t<type>, ::verilator_utils::task>;
}  // namespace verilator_utils::detail

export namespace verilator_utils
{
    /**
     * @brief 基于协程的电路评估调度器
     *
     */
    struct eval_scheduler
    {
        /**
         * @brief 评估阶段枚举
         *
         */
        enum class eval_stage_enum : ::std::uint8_t
        {
            /// 尚未开始评估
            not_begin,
            /// 评估已就绪任务
            eval_ready_task,
            /// 电路评估前
            before_dut_eval,
            /// 评估电路中，该阶段不进行协程调度
            on_dut_eval,
            /// 电路评估后
            after_dut_eval,
            /// 一轮评估完成
            eval_end
        };
        /// 协程柄类型
        using handle_t = ::std::coroutine_handle<::verilator_utils::task::promise_type>;

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
        inline static void resume_coroutine(handle_t handle)
        {
            handle.resume();
            if(handle.done())
            {
                auto&& promise{handle.promise()};
                // 借用task的raii确保在异常时销毁handle
                // 只有协程为根协程时才销毁，否则填入空句柄
                ::verilator_utils::task _{promise.is_root_coroutine() ? handle : nullptr};
            }
        }

        /**
         * @brief 评估等待队列，推进时间步，将就绪协程放入就绪队列
         *
         */
        inline void wait_queue_eval()
        {
            if(!wait_queue.empty())
            {
                auto target_time{wait_queue.top().target_time};
                // 推进时间步
                dut->contextp()->time(target_time);
                // 将就绪协程放入就绪队列
                while(!wait_queue.empty())
                {
                    if(auto&& [task_target_time, handle]{wait_queue.top()}; task_target_time == target_time)
                    {
                        ready_queue.emplace(handle);
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
        inline bool event_queue_eval()
        {
            bool any_coroutine_ready{};
            for(auto iter{event_queue.begin()}, end{event_queue.end()}; iter != end;)
            {
                if(iter->is_ready())
                {
                    ready_queue.emplace(iter->handle);
                    *iter = ::std::move(event_queue.back());
                    event_queue.pop_back();
                    end = event_queue.end();
                    any_coroutine_ready = true;
                }
                else
                {
                    ++iter;
                }
            }

            return any_coroutine_ready;
        }

        /**
         * @brief 评估就绪队列
         *
         */
        inline void ready_queue_eval()
        {
            while(!ready_queue.empty())
            {
                try
                {
                    resume_coroutine(ready_queue.top());
                }
                catch(...)
                {
                    ready_queue.pop();
                    throw;
                }
                ready_queue.pop();
            }
        }

    public:
        /**
         * @brief 构造调度器对象
         *
         * @tparam dut_t 待测模型类型，必须派生自VerilatedModel
         * @param dut 指向待测模型对象的指针
         * @note 调度器会缓存time precision，因此在构造时需要确保dut的time precision已经设置
         */
        template <::std::derived_from<::VerilatedModel> dut_t>
        inline eval_scheduler(dut_t& dut) noexcept
        {
            this->dut = &dut;
            dut_eval = [](::VerilatedModel* dut) { static_cast<dut_t*>(dut)->eval(); };
            auto&& context{*dut.contextp()};
            auto time_precision{context.timeprecision()};
            auto time_unit{context.timeunit()};
            time_precision_fs = static_cast<::std::uint64_t>(::std::pow(10, 15 + time_precision));
            time_precision_per_time_unit = static_cast<::std::uint64_t>(::std::pow(10, time_unit - time_precision));
        }

        /**
         * @brief 检查调度器是否为空
         *
         * @return 调度器是否为空
         */
        inline bool empty() const noexcept { return wait_queue.empty() && event_queue.empty() && ready_queue.empty(); }

        /**
         * @brief 检查仿真是否结束
         *
         * @return 仿真是否结束
         */
        inline bool is_finish() const noexcept { return dut->contextp()->gotFinish(); }

        /**
         * @brief 标记仿真结束
         *
         */
        inline void finish() noexcept { dut->contextp()->gotFinish(true); }

        /**
         * @brief 仿真结束时抛出eval_finish_exception异常
         *
         * @throw eval_finish_exception 若仿真已结束，抛出异常以实现协作式取消
         */
        inline void throw_if_finish() const
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
        inline dut_t& get_dut() const noexcept
        { return *static_cast<dut_t*>(dut); }

        /**
         * @brief 获取当前时间，单位为dut时间精度
         *
         * @return 当前时间
         */
        inline ::std::uint64_t time_in_time_precision() const noexcept { return dut->contextp()->time(); }

        /**
         * @brief 获取当前时间，单位为dut时间单位
         *
         * @return 当前时间
         */
        inline double time_in_time_unit() const noexcept
        { return static_cast<double>(time_in_time_precision()) / time_precision_per_time_unit; }

        /**
         * @brief 获取当前时间，已根据时间单位转换为字符串格式并添加时间单位后缀
         *
         * @return 当前时间的字符串表示
         */
        inline ::std::string time_in_string() const
        {
            auto time_in_fs{time_in_time_precision() * time_precision_fs};
            using namespace ::std::string_view_literals;
            constexpr auto unit_table{
                ::std::to_array<::std::pair<::std::uint64_t, ::std::string_view>>({
                                                                                   {1'000'000'000'000'000, "s"sv},
                                                                                   {1'000'000'000'000, "ms"sv},
                                                                                   {1'000'000'000, "us"sv},
                                                                                   {1'000'000, "ns"sv},
                                                                                   {1'000, "ps"sv},
                                                                                   {1, "fs"sv},
                                                                                   }
                 )
            };
            for(auto&& [unit, unit_str]: unit_table)
            {
                if(time_in_fs >= unit)
                {
                    return ::std::format("{:.6g}{}", static_cast<double>(time_in_fs) / static_cast<double>(unit), unit_str);
                }
            }
            for(auto&& [unit, unit_str]: unit_table)
            {
                if(time_precision_fs >= unit) { return ::std::format("0{}", unit_str); }
            }
            ::std::unreachable();
        }

        /**
         * @brief 析构调度器对象
         *
         */
        inline ~eval_scheduler() noexcept
        {
            constexpr auto do_destroy{[](handle_t handle)
                                      {
                                          if(handle.promise().is_root_coroutine()) { handle.destroy(); }
                                      }};
            while(!wait_queue.empty())
            {
                do_destroy(wait_queue.top().handle);
                wait_queue.pop();
            }

            for(auto&& [_, handle]: event_queue) { do_destroy(handle); }
            event_queue.clear();

            while(!ready_queue.empty())
            {
                do_destroy(ready_queue.top());
                ready_queue.pop();
            }
        }

        /**
         * @brief 获取调度器当前评估阶段
         *
         * @return eval_stage_enum 评估阶段枚举
         */
        inline eval_stage_enum get_eval_stage() const noexcept { return eval_stage; }

        /**
         * @brief 执行一轮评估
         *
         */
        inline void loop_once()
        {
            using enum eval_stage_enum;

            eval_stage = eval_ready_task;
            // 执行已就绪协程
            ready_queue_eval();

            // 推进时间步，执行新的就绪协程
            wait_queue_eval();
            eval_stage = before_dut_eval;
            ready_queue_eval();
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
         * @brief 循环直到调度器为空
         *
         */
        inline void loop_until_empty()
        {
            while(!empty()) { loop_once(); }
        }

        /**
         * @brief 向调度器中添加任务
         *
         * @note 这不会改变协程的调用栈，即task::promise_type::parent不会改变
         */
        inline void add_task(::verilator_utils::detail::is_task_reference auto&& task) noexcept
        { ready_queue.emplace(task.detach()); }

        /**
         * @brief 向事件队列中注册一个事件和等待该事件的协程
         *
         * @param callback 事件回调函数
         * @param handle 协程柄
         */
        inline void register_event(::verilator_utils::default_event_callback& callback, handle_t handle)
        { event_queue.emplace_back(&callback, handle); }

        /**
         * @brief 实现延迟功能的可等待体
         *
         */
        struct time_awaiter
        {
            /// 调度器对象引用
            eval_scheduler& scheduler;
            /// 目标时间点
            ::std::uint64_t target_time;

            /**
             * @brief 判断是否立即就绪
             *
             * @return false 不支持delta延迟，永远不会立即就绪
             */
            inline static bool await_ready() noexcept { return false; }

            /**
             * @brief 挂起等待，将当前任务加入等待队列
             *
             * @param handle 当前协程的句柄
             */
            inline void await_suspend(handle_t handle) const { scheduler.wait_queue.emplace(target_time, handle); }

            /**
             * @brief 恢复等待任务的执行
             *
             * @throws eval_finish_exception 若仿真已结束，抛出异常以实现协作式取消
             */
            inline void await_resume() { scheduler.throw_if_finish(); }
        };

        /**
         * @brief 等待指定时间
         *
         * @note 不支持delta延迟，等待时间不能为0
         * @param time_to_wait 等待时间，单位为时间精度，不能为0
         * @return time_awaiter 可等待体
         */
        inline time_awaiter wait_time(::std::uint64_t time_to_wait)
        {
            using namespace ::std::string_view_literals;
            REQUIRE_MESSAGE(time_to_wait != 0, "不支持delta延迟，等待时间不能为0"sv);
            return time_awaiter{*this, time_to_wait + dut->contextp()->time()};
        }

        /**
         * @brief 等待指定时间
         *
         * @param time_to_wait 等待的时间，单位为飞秒，为0表示delta延迟
         * @return time_awaiter 可等待体
         */
        inline time_awaiter wait_time(::verilator_utils::femtosecond_t time_to_wait)
        {
            using namespace ::std::string_view_literals;
            REQUIRE_MESSAGE(time_to_wait.rep != 0, "不支持delta延迟，等待时间不能为0"sv);
            auto time_to_wait_in_time_precision{time_to_wait.rep / time_precision_fs};
            REQUIRE_MESSAGE(time_to_wait_in_time_precision != 0, "等待时长小于时间精度，被截断为0"sv);
            return time_awaiter{*this, time_to_wait_in_time_precision + dut->contextp()->time()};
        }

        /**
         * @brief 实现事件触发功能的可等待体
         *
         */
        struct event_awaiter
        {
            /// 调度器对象引用
            eval_scheduler& scheduler;
            /// 事件回调，用于判断事件是否触发
            ::verilator_utils::default_event_callback event_callback;

            /**
             * @brief 判断是否立即就绪
             *
             * @return 是否立即就绪
             */
            inline bool await_ready() { return event_callback(); }

            /**
             * @brief 挂起等待，将当前任务加入事件队列
             *
             * @param handle 当前协程的句柄
             */
            inline void await_suspend(handle_t handle) { scheduler.event_queue.emplace_back(&event_callback, handle); }

            /**
             * @brief 恢复等待任务的执行
             *
             * @throws eval_finish_exception 若仿真已结束，抛出异常以实现协作式取消
             */
            inline void await_resume() { scheduler.throw_if_finish(); }
        };

        /**
         * @brief 等待事件触发
         *
         * @tparam callback_t 事件回调类型
         * @param callback 事件回调函数
         * @return event_awaiter 可等待体
         */
        template <::verilator_utils::is_event_callback callback_t>
        inline event_awaiter wait_event(callback_t&& callback)
        { return event_awaiter{*this, ::std::forward<callback_t>(callback)}; }

        /**
         * @brief 等待特定评估阶段
         *
         * @param target_stage 目标评估阶段
         * @return event_awaiter 可等待体
         *
         * @code {.cpp}
         * task foo(eval_scheduler& scheduler)
         * {
         *     // 明确等待到电路评估后，用于对电路的输出进行检查
         *     co_await scheduler.wait_stage(eval_scheduler::eval_stage_enum::after_dut_eval);
         * }
         * @endcode
         */
        inline event_awaiter wait_stage(eval_stage_enum target_stage)
        {
            return wait_event([this, target_stage] { return eval_stage == target_stage; });
        }

        /**
         * @brief 等待上升沿
         *
         * @param callback 事件回调函数
         * @param edge_to_wait 要等待到边沿数量
         * @return event_awaiter 可等待体
         */
        inline event_awaiter wait_posedge(::verilator_utils::default_event_callback callback, ::std::size_t edge_to_wait = 1)
        {
            REQUIRE_NE(edge_to_wait, 0);
            using edge_detector_t = ::verilator_utils::edge_detector;
            return wait_event(
                [edge_detector = edge_detector_t{::std::move(callback), edge_detector_t::rising}, edge_to_wait] mutable
                {
                    edge_to_wait -= edge_detector();
                    return edge_to_wait == 0;
                });
        }

        /**
         * @brief 等待上升沿
         *
         * @param bit 信号位切片
         * @param edge_to_wait 要等待到边沿数量
         * @return event_awaiter 可等待体
         */
        inline event_awaiter wait_posedge(auto&& bit, ::std::size_t edge_to_wait = 1)
            requires (::verilator_utils::is_bit_slice<::std::remove_cvref_t<decltype(bit)>>)
        {
            return wait_posedge([bit] { return static_cast<bool>(bit); }, edge_to_wait);
        }

        /**
         * @brief 等待下降沿
         *
         * @param callback 事件回调函数
         * @param edge_to_wait 要等待到边沿数量
         * @return event_awaiter 可等待体
         */
        inline event_awaiter wait_negedge(::verilator_utils::default_event_callback callback, ::std::size_t edge_to_wait = 1)
        {
            REQUIRE_NE(edge_to_wait, 0);
            using edge_detector_t = ::verilator_utils::edge_detector;
            return wait_event(
                [edge_detector = edge_detector_t{::std::move(callback), edge_detector_t::falling}, edge_to_wait] mutable
                {
                    edge_to_wait -= edge_detector();
                    return edge_to_wait == 0;
                });
        }

        /**
         * @brief 等待下降沿
         *
         * @param bit 信号位切片
         * @param edge_to_wait 要等待到边沿数量
         * @return event_awaiter 可等待体
         */
        inline event_awaiter wait_negedge(auto&& bit, ::std::size_t edge_to_wait = 1)
            requires (::verilator_utils::is_bit_slice<::std::remove_cvref_t<decltype(bit)>>)
        {
            return wait_negedge([bit] { return static_cast<bool>(bit); }, edge_to_wait);
        }

        /**
         * @brief 等待双边沿
         *
         * @param callback 事件回调函数
         * @param edge_to_wait 要等待到边沿数量
         * @return event_awaiter 可等待体
         */
        inline event_awaiter wait_alledge(::verilator_utils::default_event_callback callback, ::std::size_t edge_to_wait = 1)
        {
            REQUIRE_NE(edge_to_wait, 0);
            using edge_detector_t = ::verilator_utils::edge_detector;
            return wait_event(
                [edge_detector = edge_detector_t{::std::move(callback), edge_detector_t::both}, edge_to_wait] mutable
                {
                    edge_to_wait -= edge_detector();
                    return edge_to_wait == 0;
                });
        }

        /**
         * @brief 等待双边沿
         *
         * @param bit 信号位切片
         * @param edge_to_wait 要等待到边沿数量
         * @return event_awaiter 可等待体
         */
        inline event_awaiter wait_alledge(auto&& bit, ::std::size_t edge_to_wait = 1)
            requires (::verilator_utils::is_bit_slice<::std::remove_cvref_t<decltype(bit)>>)
        {
            return wait_alledge([bit] { return static_cast<bool>(bit); }, edge_to_wait);
        }
    };
}  // namespace verilator_utils

export namespace verilator_utils
{
    /**
     * @brief 生成时钟信号，占空比为50%
     *
     * @param scheduler 调度器引用
     * @param clk 时钟信号切片
     * @param period 时钟周期
     * @return 生成时钟信号的任务
     */
    inline ::verilator_utils::task generate_clock(::verilator_utils::eval_scheduler& scheduler,
                                                  ::verilator_utils::bit_slice<::CData> clk,
                                                  ::verilator_utils::femtosecond_t period)
    {
        auto half_period{period.rep / 2};
        clk = 0;
        while(true)
        {
            co_await scheduler.wait_time(half_period);
            clk = !clk;
        }
    }

    /**
     * @brief 生成复位信号，高电平有效，在cycle个下降沿后跳变为低电平
     *
     * @param scheduler 调度器引用
     * @param reset 复位信号引用
     * @param clk 时钟信号引用
     * @param cycle 复位信号持续的下降沿数
     * @return 生成复位信号的任务
     */
    inline ::verilator_utils::task generate_reset(::verilator_utils::eval_scheduler& scheduler,
                                                  ::verilator_utils::bit_slice<::CData> reset,
                                                  ::verilator_utils::bit_slice<::CData> clk,
                                                  ::size_t cycle = 3)
    {
        reset = 1;
        co_await scheduler.wait_negedge(clk, cycle);
        reset = 0;
    }

    /**
     * @brief 生成复位信号，低电平有效，在cycle个下降沿后跳变为高电平
     *
     * @param scheduler 调度器引用
     * @param reset 复位信号引用
     * @param clk 时钟信号引用
     * @param cycle 复位信号持续的下降沿数
     * @return 生成复位信号的任务
     */
    inline ::verilator_utils::task generate_reset_n(::verilator_utils::eval_scheduler& scheduler,
                                                    ::verilator_utils::bit_slice<::CData> reset,
                                                    ::verilator_utils::bit_slice<::CData> clk,
                                                    ::size_t cycle = 3)
    {
        reset = 0;
        co_await scheduler.wait_negedge(clk, cycle);
        reset = 1;
    }

    /**
     * @brief 异步任务类型
     *
     */
    struct async_task
    {
        /// 协程柄类型
        using handle_t = ::std::coroutine_handle<::verilator_utils::task::promise_type>;

        /**
         * @brief 将同步任务转化为异步任务，并将任务添加到调度器就绪队列
         *
         * @note 任务必须处于initial_suspend状态
         * @param scheduler 调度器引用
         * @param task 同步任务对象
         */
        inline async_task(::verilator_utils::eval_scheduler& scheduler,
                          ::verilator_utils::detail::is_task_reference auto&& task) :
            callback{[subhandle = task.get_handle()] { return !subhandle || subhandle.done(); }}, scheduler{scheduler},
            subhandle{task.get_handle()}
        {
            REQUIRE_EQ(task.get_promise().status, ::verilator_utils::task::status_enum::initial_suspend);
            task.get_promise().parent = ::std::noop_coroutine();
            scheduler.add_task(task);
        }

        /**
         * @brief 析构异步任务对象并销毁绑定的子协程柄
         *
         */
        inline ~async_task() { destroy(); }

        /**
         * @brief 销毁绑定的协程柄
         *
         */
        inline void destroy()
        {
            if(subhandle) { ::std::exchange(subhandle, nullptr).destroy(); }
        }

        /**
         * @brief 获取任务的协程句柄
         *
         * @return 任务的协程句柄
         */
        inline handle_t get_handle() const { return subhandle; }

        /**
         * @brief 获取任务的promise引用
         *
         * @return 任务的promise引用
         */
        inline ::verilator_utils::task::promise_type& get_promise() const noexcept { return subhandle.promise(); }

        /**
         * @brief 获取绑定的调度器引用
         *
         * @return eval_scheduler& 调度器引用
         */
        inline ::verilator_utils::eval_scheduler& get_scheduler() { return scheduler; }

        /**
         * @brief 判断子任务是否执行完
         *
         * @note 未绑定到子任务视为执行完
         * @return 子任务是否执行完
         */
        inline bool done() const noexcept { return !subhandle || subhandle.done(); }

        /**
         * @brief 分离异步任务的协程柄，此后异步任务不再持有该协程柄
         *
         * @return 异步任务的协程柄
         */
        inline handle_t detach() noexcept { return ::std::exchange(subhandle, nullptr); }

        /**
         * @brief 检查任务对象是否绑定了协程柄
         *
         * @return 是否绑定了协程柄
         */
        inline explicit operator bool() const noexcept { return static_cast<bool>(subhandle); }

        /**
         * @brief 判断是否立即完成
         *
         * @return 子任务已执行完则立即完成
         */
        inline bool await_ready() const { return done(); }

        /**
         * @brief 向调度器事件队列中注册等待事件，然后挂起协程
         *
         * @param handle 当前任务的协程柄
         */
        inline void await_suspend(handle_t handle) { scheduler.register_event(callback, handle); }

        /**
         * @brief 恢复等待任务的执行
         *
         * @throws eval_finish_exception 若仿真已结束，抛出异常以实现协作式取消
         * @throws 若子任务抛出异常则重新抛出异常
         */
        inline void await_resume()
        {
            // 协作式取消的优先级更高
            scheduler.throw_if_finish();
            subhandle.promise().rethrow_exception();
        }

    private:
        /// 事件回调函数，判断子任务是否完成
        ::verilator_utils::default_event_callback callback;
        /// 调度器引用
        ::verilator_utils::eval_scheduler& scheduler;
        /// 子任务的协程柄
        handle_t subhandle;
    };

    /**
     * @brief 等待所有异步任务完成用的可等待体
     *
     */
    struct task_join_awaiter
    {
        inline task_join_awaiter(::std::span<::verilator_utils::async_task> tasks, ::verilator_utils::eval_scheduler& scheduler) :
            callback{[tasks] { return all_tasks_done(tasks); }}, tasks{tasks}, scheduler{scheduler}
        {
        }

        /**
         * @brief 判断是否立即完成
         *
         * @return 所有子任务都已执行完则立即完成
         */
        inline bool await_ready() { return callback(); }

        /**
         * @brief 向调度器事件队列中注册等待事件，然后挂起协程
         *
         * @param handle 当前任务的协程柄
         */
        inline void await_suspend(::verilator_utils::async_task::handle_t handle) { scheduler.register_event(callback, handle); }

        /// 储存子任务中未处理的异常
        using unhandled_exception_pair = ::std::pair<::verilator_utils::async_task&, ::std::exception_ptr>;
        /// 若子任务存在未处理的异常则抛出该向量
        using unhandled_exception_vector = ::std::vector<unhandled_exception_pair>;

        /**
         * @brief 恢复等待任务的执行
         *
         * @throws eval_finish_exception 若仿真已结束，抛出异常以实现协作式取消
         * @throws unhandled_exception_vector 子任务中未处理的异常
         */
        inline void await_resume()
        {
            scheduler.throw_if_finish();
            auto vec{::std::views::filter(tasks,
                                          [](::verilator_utils::async_task& task) static noexcept
                                          { return task.get_promise().with_unhandled_exception(); }) |
                     ::std::views::transform([](::verilator_utils::async_task& task) static noexcept
                                             { return unhandled_exception_pair{task, task.get_promise().exception}; }) |
                     ::std::ranges::to<unhandled_exception_vector>()};

            if(!vec.empty())
            {
                throw vec;  // NOLINT(misc-throw-by-value-catch-by-reference)
            }
        }

    private:
        /**
         * @brief 判断是否所有子任务都执行完毕
         *
         * @return 是否所有子任务都执行完毕
         */
        inline static bool all_tasks_done(::std::span<::verilator_utils::async_task> tasks)
        {
            return ::std::ranges::all_of(tasks, [](::verilator_utils::async_task& task) { return task.done(); });
        }

        /// 事件回调函数，判断子任务是否都完成
        ::verilator_utils::default_event_callback callback;
        /// 子任务视图
        ::std::span<::verilator_utils::async_task> tasks;
        /// 调度器引用
        ::verilator_utils::eval_scheduler& scheduler;
    };

    /**
     * @brief 等待所有异步任务完成
     *
     * @param scheduler 调度器引用
     * @param tasks 异步任务视图
     * @return task_join_awaiter 可等待体
     */
    inline ::verilator_utils::task_join_awaiter task_join(::verilator_utils::eval_scheduler& scheduler,
                                                          ::std::span<::verilator_utils::async_task> tasks)
    { return ::verilator_utils::task_join_awaiter{tasks, scheduler}; }

    /**
     * @brief 等待到验证时机
     *
     * @param scheduler 调度器引用
     * @param clk 时钟信号切片
     * @param edge_to_wait 要等待到边沿个数
     * @return 可等待体
     * @note 等待到n个时钟上升沿后的after_dut_eval阶段
     */
    inline ::verilator_utils::eval_scheduler::event_awaiter wait_for_verify(::verilator_utils::eval_scheduler& scheduler,
                                                                            ::verilator_utils::bit_slice<::CData> clk,
                                                                            ::std::size_t edge_to_wait = 1)
    {
        REQUIRE_NE(edge_to_wait, 0);
        return scheduler.wait_event(
            [clk, edge_to_wait, &scheduler] mutable
            {
                ::verilator_utils::edge_detector edge_detector{[clk] { return static_cast<bool>(clk); },
                                                               ::verilator_utils::edge_detector::rising};
                if(edge_detector()) { --edge_to_wait; }
                return edge_to_wait == 0 &&
                       scheduler.get_eval_stage() == ::verilator_utils::eval_scheduler::eval_stage_enum::after_dut_eval;
            });
    }

    /**
     * @brief 等待到激励时机
     *
     * @param scheduler 调度器引用
     * @param clk 时钟信号切片
     * @param edge_to_wait 要等待到边沿个数
     * @return 可等待体
     * @note 等待到n个时钟下降沿后的before_dut_eval阶段
     */
    inline ::verilator_utils::eval_scheduler::event_awaiter wait_for_stimulus(::verilator_utils::eval_scheduler& scheduler,
                                                                              ::verilator_utils::bit_slice<::CData> clk,
                                                                              ::std::size_t edge_to_wait = 1)
    { return scheduler.wait_negedge(clk, edge_to_wait); }
}  // namespace verilator_utils
