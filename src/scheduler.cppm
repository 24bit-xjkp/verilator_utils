export module verilator_utils:scheduler;
import :wrapper;

namespace
{
    using namespace ::std::string_view_literals;
}

export namespace verilator_utils
{
    /**
     * @brief 仿真结束异常类，用于实现协作式取消
     *
     * @note 该异常在框架中使用，不要在框架外捕获它
     */
    struct eval_finish_exception : ::std::runtime_error
    {
        eval_finish_exception() noexcept : ::std::runtime_error{"仿真正常退出"} {}
    };

    /**
     * @brief 任务取消异常类，用于实现协作式取消
     *
     * @note 该异常在框架中使用，不要在框架外捕获它
     */
    struct task_cancel_exception : ::std::runtime_error
    {
        task_cancel_exception() noexcept : ::std::runtime_error{"任务取消"} {}
    };

    /**
     * @brief 子任务取消，用于向父任务报告子任务已取消
     *
     * @note 未捕获时，该异常会沿调用链向上传播
     */
    struct subtask_cancel_exception : ::std::runtime_error
    {
        subtask_cancel_exception() noexcept : ::std::runtime_error{"子任务取消"} {}
    };

    /**
     * @brief 仿真超时异常类
     *
     */
    struct eval_timeout_exception : ::std::runtime_error
    {
        eval_timeout_exception() noexcept : ::std::runtime_error{"仿真超时退出"} {}
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
    concept is_coroutine_promise = ::std::derived_from<promise_type, ::verilator_utils::detail::promise_base> &&
                                   !::std::same_as<promise_type, ::verilator_utils::detail::promise_base>;
}  // namespace verilator_utils

namespace verilator_utils::detail
{
    template <typename type>
    constexpr bool is_coroutine_handle_impl{};

    template <typename type>
    constexpr bool is_coroutine_handle_impl<::std::coroutine_handle<type>>{true};

    /**
     * @brief 判断类型type是否为协程柄
     *
     * @tparam type 要判断的类型
     */
    template <typename type>
    concept is_coroutine_handle = ::verilator_utils::detail::is_coroutine_handle_impl<type>;

    /**
     * @brief 判断类型type是否为可等待体的await_suspend成员函数的返回类型
     *
     * @tparam type 要判断的类型
     */
    template <typename type>
    concept is_await_suspend_return_type =
        ::verilator_utils::same_as_any<type, void, bool> || ::verilator_utils::detail::is_coroutine_handle<type>;

    /**
     * @brief 判断类型type是否为可等待体
     *
     * @tparam type 要判断的类型
     * @tparam promise_type 承诺类型
     */
    template <typename type, typename promise_type>
    concept is_awaiter = ::verilator_utils::is_coroutine_promise<promise_type> &&
                         requires(type&& self, ::std::coroutine_handle<promise_type> handle) {
                             { self.await_ready() } -> ::std::same_as<bool>;
                             { self.await_suspend(handle) } -> ::verilator_utils::detail::is_await_suspend_return_type;
                             self.await_resume();
                         };

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
        /// 协程正在执行
        running,
        /// 协程已挂起，等待调度执行
        suspended,
        /// 协程收到取消请求
        cancel_requested,
        /// 协程收到评估结束请求
        eval_finish_requested,
        /// 协程已取消
        canceled,
        /// 协程异常退出
        aborted,
        /// 协程执行完毕
        finished,
    };

    /**
     * @brief 向需要挂起的可等待体中注册挂起点的源代码位置信息
     *
     * @tparam awaiter_t 可等待体
     */
    template <typename awaiter_t>
    struct awaiter_wrapper;

    /**
     * @brief 协程种类枚举
     *
     */
    enum class coroutine_type_enum : ::std::uint8_t
    {
        /// 没有父协程
        without_parent = 0,
        /// 有父协程
        with_parent = 1,
        /// 同步协程
        is_sync = 0,
        /// 异步协程
        is_async = 2,

        /// 根协程
        root_coroutine = without_parent | is_sync,
        /// 同步子协程
        sub_coroutine = with_parent | is_sync,
        /// 异步协程
        async_coroutine = is_async
    };

    constexpr bool operator& (coroutine_type_enum value, coroutine_type_enum mask) noexcept
    { return (::std::to_underlying(value) & ::std::to_underlying(mask)) != 0; }

    // 导出coroutine_pair、promise_base和promise_with_return以支持在verilator_utils模块外扩展任务类型

    export struct promise_base;

    /**
     * @brief 协程状态对，包含协程柄和类型擦除的承诺指针
     *
     */
    export struct coroutine_pair
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
        coroutine_pair(const ::verilator_utils::detail::promise_base& subtask_promise) noexcept;

        coroutine_pair(::std::coroutine_handle<> handle = nullptr,
                       ::verilator_utils::detail::promise_base* promise = nullptr) noexcept : handle{handle}, promise{promise}
        {
        }

        friend bool operator== (const coroutine_pair& lhs, ::std::nullptr_t /* unused */) noexcept
        {
            // 由于协程柄和承诺指针指代同一个协程，因此比较一个即可
            return lhs.handle == nullptr;
        }

        friend bool operator== (const coroutine_pair& lhs, const coroutine_pair& rhs) noexcept
        {
            // 由于协程柄和承诺指针指代同一个协程，因此比较一个即可
            return lhs.handle == rhs.handle;
        }

        friend ::std::strong_ordering operator<=> (const coroutine_pair& lhs, const coroutine_pair& rhs) noexcept
        {
            // 由于协程柄和承诺指针指代同一个协程，因此比较一个即可
            return lhs.handle <=> rhs.handle;
        }

        // NOLINTEND(*-explicit-constructor)
    };
}  // namespace verilator_utils::detail

export namespace verilator_utils
{
    /**
     * @brief 协程栈回溯
     *
     */
    struct coroutine_stacktrace
    {
        /// 协程栈帧
        struct frame
        {
            /// 指向协程帧的指针
            void* coroutine_frame_ptr{};
            /// 协程挂起位置
            ::std::source_location location{};
            /// 协程类型
            ::verilator_utils::detail::coroutine_type_enum type{};

            frame() noexcept = default;

            explicit frame(::verilator_utils::detail::coroutine_pair pair) noexcept;

            explicit frame(void* coroutine_frame_ptr,
                           ::std::source_location location,
                           ::verilator_utils::detail::coroutine_type_enum type) noexcept :
                coroutine_frame_ptr{coroutine_frame_ptr}, location{location}, type{type}
            {
            }
        };

        /// 协程栈帧数组
        ::std::vector<frame> frames{};

        /**
         * @brief 创建一个空的协程栈回溯对象
         *
         */
        coroutine_stacktrace() noexcept = default;

        /**
         * @brief 创建一个协程栈回溯对象
         *
         * @param pair 协程状态对
         */
        explicit coroutine_stacktrace(::verilator_utils::detail::coroutine_pair pair) :
            frames{backtrace(pair) | ::std::ranges::to<::std::vector>()}
        {
        }

        /**
         * @brief 判断栈帧数组是否为空
         *
         * @return 栈帧数组是否为空
         */
        [[nodiscard]] bool empty() const noexcept { return frames.empty(); }

        /**
         * @brief 开始协程栈回溯
         *
         * @param pair 协程状态对
         * @return 协程栈帧生成器
         */
        static ::verilator_utils::generator<frame> backtrace(::verilator_utils::detail::coroutine_pair pair);
    };

    /**
     * @brief 协程异常类型，带有协程栈回溯信息
     *
     */
    struct coroutine_exception : ::std::exception
    {
        /**
         * @brief 构造协程异常对象
         *
         * @param exception 原始异常指针
         * @param stacktrace 协程栈回溯
         */
        explicit coroutine_exception(::std::exception_ptr exception,
                                     ::verilator_utils::coroutine_stacktrace stacktrace = {}) noexcept :
            exception_{::std::move(exception)}, stacktrace_{::std::move(stacktrace)},
            message{generate_message()}
        {
        }

        /**
         * @brief 获取原始异常指针
         *
         * @return 原始异常指针的引用
         */
        [[nodiscard]] const ::std::exception_ptr& exception() const noexcept { return exception_; }

        /**
         * @brief 获取协程栈回溯
         *
         * @return 协程栈回溯引用
         */
        [[nodiscard]] const ::verilator_utils::coroutine_stacktrace& stacktrace() const noexcept { return stacktrace_; }

        /**
         * @brief 重新抛出原始异常
         *
         */
        void rethrow_exception() const { ::std::rethrow_exception(exception_); }

        [[nodiscard]] const char* what() const noexcept override { return message.c_str(); }

    private:
        ::std::exception_ptr exception_;
        ::verilator_utils::coroutine_stacktrace stacktrace_;
        ::std::string message;

        ::std::string generate_message() noexcept;
    };
}  // namespace verilator_utils

namespace verilator_utils::detail
{
    /**
     * @brief 协程承诺类型的基类
     *
     */
    export struct promise_base
    {
        /// 协程状态枚举
        using status_enum = ::verilator_utils::detail::status_enum;
        /// 异常指针
        ::std::exception_ptr exception{};
        ::verilator_utils::detail::coroutine_pair parent{};
        /// 调度器指针，用于实现隐式的调度器传递
        ::verilator_utils::eval_scheduler* scheduler{};
        /// 协程挂起点的源代码位置
        ::std::source_location suspend_location{};
        /// 协程状态
        status_enum status{status_enum::creating};
        /// 是否为异步协程
        /// - 为false表示同步协程，执行完毕后立即跳转到父协程
        /// - 为true表示异步协程
        bool is_async{};

        /// 协程种类枚举
        using coroutine_type_enum = ::verilator_utils::detail::coroutine_type_enum;

        /// - 无父的同步协程为根协程，生命周期由调度器管理
        /// - 有父的同步协程为同步子协程，生命周期由父协程的task对象管理
        /// - 异步协程为异步协程，生命周期由父协程的async_task对象管理

    private:
        struct initial_awaiter : ::std::suspend_always
        {
            promise_base& promise;

            void await_resume();
        };

        struct final_awaiter : ::std::suspend_always
        {
            promise_base& promise;

            [[nodiscard]] ::std::coroutine_handle<> await_suspend(::std::coroutine_handle<> handle) const noexcept;
        };

    public:
        /**
         * @brief 协程初始挂起
         *
         * @return 可等待体，总是挂起协程
         */
        initial_awaiter initial_suspend() noexcept
        {
            status = status_enum::initial_suspend;
            return {.promise = *this};
        }

        /**
         * @brief 协程最终挂起
         *
         * @return 挂起协程，若存在父协程则跳转到父协程执行，
         */
        final_awaiter final_suspend() noexcept { return {.promise = *this}; }

        /**
         * @brief 判断协程的种类
         *
         * @return 协程种类枚举
         */
        [[nodiscard]] coroutine_type_enum classify() const noexcept
        {
            using enum coroutine_type_enum;
            if(this->is_async) { return async_coroutine; }
            return parent == nullptr ? root_coroutine : sub_coroutine;
        }

        /**
         * @brief 检查任务是否绑定到调度器
         *
         * @return 已绑定则返回调度器指针，否则断言失败
         */
        [[nodiscard]] ::verilator_utils::eval_scheduler* check_scheduler() const
        {
            ::verilator_utils::check{}(scheduler != nullptr, "任务必须绑定调度器"sv);
            return scheduler;
        }

        /**
         * @brief 将协程中抛出的异常存储到异常指针中
         *
         */
        void unhandled_exception(::std::source_location location = ::std::source_location::current()) noexcept
        {
            // 复用suspend_location来表示协程内异常抛出位置
            suspend_location = location;
            try
            {
                throw;
            }
            catch(const ::verilator_utils::eval_finish_exception&)
            {
                status = status_enum::eval_finish_requested;
            }
            catch(const ::verilator_utils::task_cancel_exception&)  // NOLINT(bugprone-empty-catch)
            {
                // 保持取消请求，以便final_suspend将状态置为canceled而不是aborted
                status = status_enum::cancel_requested;
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
        [[nodiscard]] bool with_unhandled_exception() const noexcept { return static_cast<bool>(exception); }

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
         * @brief 判断协程是否正常结束
         *
         * @return 是否已正常结束
         */
        [[nodiscard]] bool is_coroutine_finished() const noexcept { return status == status_enum::finished; }

        /**
         * @brief 判断协程是否退出，不论退出方式
         *
         * @return 协程是否退出
         */
        [[nodiscard]] bool is_coroutine_exited() const noexcept
        { return status == status_enum::finished || status == status_enum::aborted || status == status_enum::canceled; }

        /**
         * @brief 转发可等待体
         *
         * @tparam awaiter_t 可等待体类型
         * @param awaiter 可等待体对象
         * @return 转发的可等待体对象
         */
        template <::verilator_utils::is_coroutine_promise promise_type, typename awaiter_t>
        decltype(auto) await_transform(this promise_type& self, awaiter_t&& awaiter)
        {
            using pure_awaiter_t = ::std::remove_cvref_t<awaiter_t>;
            if constexpr(::std::derived_from<pure_awaiter_t, ::verilator_utils::detail::no_suspend_awaiter>)
            {
                // 通过set_handle向可等待体传递协程柄
                awaiter.set_handle(::std::coroutine_handle<promise_type>::from_promise(self));
                return ::std::forward<awaiter_t>(awaiter);
            }
            else if constexpr(::std::derived_from<pure_awaiter_t, ::std::suspend_never>)
            {
                // 不需要等待的可等待体直接转发
                return ::std::forward<awaiter_t>(awaiter);
            }
            else if constexpr(requires(awaiter_t&& awaiter) {
                                  { awaiter.operator co_await() } -> ::verilator_utils::detail::is_awaiter<promise_type>;
                              })
            {
                return ::verilator_utils::detail::awaiter_wrapper{::std::forward<awaiter_t>(awaiter).operator co_await(), self};
            }
            else if constexpr(requires(awaiter_t&& awaiter) {
                                  { operator co_await(awaiter) } -> ::verilator_utils::detail::is_awaiter<promise_type>;
                              })
            {
                return ::verilator_utils::detail::awaiter_wrapper{operator co_await(::std::forward<awaiter_t>(awaiter)), self};
            }
            else if constexpr(::verilator_utils::detail::is_awaiter<awaiter_t, promise_type>)
            {
                return ::verilator_utils::detail::awaiter_wrapper{::std::forward<awaiter_t>(awaiter), self};
            }
            else
            {
                static_assert(false, "未知的可等待体类型");
                return ::std::suspend_never{};
            }
        }

        /**
         * @brief 判断任务是否可取消
         *
         * @return 是否可取消
         */
        [[nodiscard]] bool cancel_possible() const noexcept
        { return status == status_enum::initial_suspend || status == status_enum::suspended; }

        /**
         * @brief 取消任务
         *
         */
        void cancel()
        {
            ::verilator_utils::check{}(cancel_possible(), "当前任务状态为{}，不可取消"sv, ::std::to_underlying(status));
            status = status_enum::cancel_requested;
        }

        /**
         * @brief 判断任务是否收到取消请求
         *
         * @return 是否收到取消请求
         */
        [[nodiscard]] bool cancel_requested() const noexcept { return status == status_enum::cancel_requested; }
    };

    ::verilator_utils::detail::coroutine_pair::coroutine_pair(
        const ::verilator_utils::detail::promise_base& subtask_promise) noexcept : coroutine_pair{subtask_promise.parent}
    {
    }

    template <typename awaiter_t>
    struct awaiter_wrapper
    {
        awaiter_t awaiter;
        promise_base& promise;

        bool await_ready() noexcept(noexcept(awaiter.await_ready())) { return awaiter.await_ready(); }

        template <::verilator_utils::is_coroutine_promise promise_type>
        auto await_suspend(
            ::std::coroutine_handle<promise_type> handle,
            ::std::source_location location = ::std::source_location::current()) noexcept(noexcept(awaiter.await_suspend(handle)))
        {
            if constexpr(::std::same_as<decltype(awaiter.await_suspend(handle)), bool>)
            {
                auto need_suspend{awaiter.await_suspend(handle)};
                if(need_suspend)
                {
                    promise.suspend_location = location;
                    promise.status = ::verilator_utils::detail::status_enum::suspended;
                }
                return need_suspend;
            }
            else
            {
                promise.suspend_location = location;
                promise.status = ::verilator_utils::detail::status_enum::suspended;
                return awaiter.await_suspend(handle);
            }
        }

        decltype(auto) await_resume();
    };

    /**
     * @brief 协程返回值实现
     *
     * @tparam return_t 返回类型
     */
    export template <typename return_t>
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
            requires (::std::constructible_from<return_type, value_type&&>)
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
        using handle_t = ::std::coroutine_handle<promise_type>;
        using return_type = promise_type::return_type;
        /// 子任务的协程句柄
        handle_t subhandle;

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
        template <::verilator_utils::is_coroutine_promise promise_t>
        [[nodiscard]] handle_t await_suspend(::std::coroutine_handle<promise_t> parent) const noexcept
        {
            subhandle.promise().parent = parent;
            subhandle.promise().scheduler = parent.promise().scheduler;
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
}  // namespace verilator_utils::detail

export namespace verilator_utils::detail
{
    /**
     * @brief 协程栈帧指针字段的宽度
     *
     * 与Stack trace的地址字段一致，按指针的十六进制位数（含0x前缀）取固定宽度，
     * 使得高位补0后各帧的指针字段等宽
     */
    constexpr inline ::std::size_t coroutine_frame_pointer_width{2 * sizeof(void*) + 2};

    /**
     * @brief 获取协程类型的名称
     *
     * @param type 协程类型
     * @return 协程类型名称
     */
    [[nodiscard]] constexpr ::std::string_view
        coroutine_type_name(::verilator_utils::detail::promise_base::coroutine_type_enum type) noexcept
    {
        using enum ::verilator_utils::detail::promise_base::coroutine_type_enum;
        switch(type)
        {
            case root_coroutine: return "root"sv;
            case sub_coroutine: return "sub"sv;
            case async_coroutine: return "async"sv;
        }
        ::std::unreachable();
    }

    /**
     * @brief 协程类型字段的宽度，含括号
     *
     * 取所有协程类型名称中最宽者，格式化时在类型字段右侧补空格即可使各帧的in关键字对齐
     * @return 协程类型字段的宽度
     */
    [[nodiscard]] consteval ::std::size_t coroutine_type_field_width() noexcept
    {
        using enum ::verilator_utils::detail::promise_base::coroutine_type_enum;
        ::std::size_t width{};
        for(const auto type: {root_coroutine, sub_coroutine, async_coroutine})
        {
            width = ::std::max(width, coroutine_type_name(type).size() + 2);
        }
        return width;
    }
}  // namespace verilator_utils::detail

export namespace std
{
    template <>
    struct formatter<::verilator_utils::detail::promise_base::coroutine_type_enum>
    {
        constexpr static auto parse(::std::format_parse_context& ctx)
        {
            return ::verilator_utils::detail::parse_format_string_without_flags(
                ctx,
                "无效的verilator_utils::detail::promise_base::coroutine_type_enum格式符"sv);
        }

        template <typename iter_t>
        static auto format(::verilator_utils::detail::promise_base::coroutine_type_enum value,
                           ::std::basic_format_context<iter_t, char>& ctx)
        { return ::std::format_to(ctx.out(), "{}"sv, ::verilator_utils::detail::coroutine_type_name(value)); }
    };

    /**
     * @brief 协程栈帧格式化支持
     *
     * 布局与Stack trace的栈帧一致：指针值按指针宽度补0，协程类型字段右侧补空格，
     * 使得各帧的类型字段等宽、指针字段与in关键字对齐；
     * 各字段的着色也与Stack trace一致：指针和行列号为蓝色，函数名为黄色，文件路径为绿色
     * 支持的格式符：
     * - #: 输出带ANSI颜色的协程栈帧，颜色使用方式与断言消息一致
     */
    template <>
    struct formatter<::verilator_utils::coroutine_stacktrace::frame>
    {
        bool with_color{};

        constexpr auto parse(::std::format_parse_context& ctx)
        {
            return ::verilator_utils::detail::parse_format_string_with_detail_flag(
                ctx,
                "无效的verilator_utils::coroutine_stacktrace::stacktrace_frame格式符"sv,
                with_color);
        }

        template <typename iter_t>
        auto format(const ::verilator_utils::coroutine_stacktrace::frame& value,
                    ::std::basic_format_context<iter_t, char>& ctx) const
        {
            using namespace ::verilator_utils::detail::assertion_color;
            constexpr auto type_field_width{::verilator_utils::detail::coroutine_type_field_width()};
            const auto type_name{::verilator_utils::detail::coroutine_type_name(value.type)};
            const auto out{::std::format_to(ctx.out(),
                                            "{}{:#0{}x}{} ({}){:{}}"sv,
                                            with_color ? blue : none,
                                            ::std::bit_cast<::std::uintptr_t>(value.coroutine_frame_ptr),
                                            ::verilator_utils::detail::coroutine_frame_pointer_width,
                                            with_color ? reset : none,
                                            type_name,
                                            ""sv,
                                            type_field_width - (type_name.size() + 2))};
            return ::std::format_to(out,
                                    " in {}{}{} at {}{}{}:{}{}{}:{}{}{}"sv,
                                    with_color ? yellow : none,
                                    value.location.function_name(),
                                    with_color ? reset : none,
                                    with_color ? green : none,
                                    value.location.file_name(),
                                    with_color ? reset : none,
                                    with_color ? blue : none,
                                    value.location.line(),
                                    with_color ? reset : none,
                                    with_color ? blue : none,
                                    value.location.column(),
                                    with_color ? reset : none);
        }
    };

    /**
     * @brief 协程栈回溯格式化支持
     *
     * 支持的格式符：
     * - #: 输出带ANSI颜色的协程栈回溯，颜色使用方式与断言消息一致
     */
    template <>
    struct formatter<::verilator_utils::coroutine_stacktrace>
    {
        bool with_color{};

        constexpr auto parse(::std::format_parse_context& ctx)
        {
            return ::verilator_utils::detail::parse_format_string_with_detail_flag(
                ctx,
                "无效的verilator_utils::coroutine_stacktrace格式符"sv,
                with_color);
        }

        template <typename iter_t>
        auto format(const ::verilator_utils::coroutine_stacktrace& value, ::std::basic_format_context<iter_t, char>& ctx) const
        {
            auto out{::std::format_to(ctx.out(),
                                      "{}Coroutine stack trace (most recent call first):\n"sv,
                                      with_color ? ::verilator_utils::detail::assertion_color::reset : ""sv)};
            // 与Stack trace一致，帧编号按总条目数自适应宽度，使各帧的指针字段对齐
            const auto number_width{value.frames.empty() ? 1zu : ::std::to_string(value.frames.size() - 1).size()};
            for(auto&& [i, frame]: value.frames | ::std::views::enumerate)
            {
                out = with_color ? ::std::format_to(out, "#{:<{}} {:#}\n"sv, i, number_width, frame)
                                 : ::std::format_to(out, "#{:<{}} {}\n"sv, i, number_width, frame);
            }
            return out;
        }
    };
}  // namespace std

namespace verilator_utils
{
    ::verilator_utils::coroutine_stacktrace::frame::frame(::verilator_utils::detail::coroutine_pair pair) noexcept :
        coroutine_frame_ptr{pair.handle.address()}, location{pair.promise->suspend_location}, type{pair.promise->classify()}
    {
    }

    auto ::verilator_utils::coroutine_stacktrace::backtrace(::verilator_utils::detail::coroutine_pair pair)
        -> ::verilator_utils::generator<frame>
    {
        while(pair != nullptr) { co_yield frame{::std::exchange(pair, pair.promise->parent)}; }
    }

    auto ::verilator_utils::coroutine_exception::generate_message() noexcept -> ::std::string
    {
        try
        {
            ::std::string_view message{};
            try
            {
                ::std::rethrow_exception(exception_);
            }
            catch(const ::std::exception& exception)
            {
                message = exception.what();
            }
            catch(...)
            {
                message = "unknown"sv;
            }
            if(stacktrace_.empty()) { return ::std::format("{}\nCoroutine stack trace unavailable"sv, message); }
            const auto use_color{::verilator_utils::detail::should_colorize_assertion_message()};
            if(use_color) { return ::std::format("{}\n{:#}"sv, message, stacktrace_); }
            return ::std::format("{}\n{}"sv, message, stacktrace_);
        }
        catch(...)
        {
            return {};
        }
    }
}  // namespace verilator_utils

export namespace doctest
{
    template <>
    struct StringMaker<::verilator_utils::coroutine_stacktrace::frame>
    {
        static ::doctest::String convert(const ::verilator_utils::coroutine_stacktrace::frame& value)
        {
            if(::verilator_utils::detail::should_colorize_assertion_message()) { return ::std::format("{:#}"sv, value); }
            return ::std::format("{}"sv, value);
        }
    };

    template <>
    struct StringMaker<::verilator_utils::coroutine_stacktrace>
    {
        static ::doctest::String convert(const ::verilator_utils::coroutine_stacktrace& value)
        {
            if(::verilator_utils::detail::should_colorize_assertion_message()) { return ::std::format("{:#}"sv, value); }
            return ::std::format("{}"sv, value);
        }
    };
}  // namespace doctest

export namespace verilator_utils
{
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
        struct promise_type  // NOLINT(cppcoreguidelines-special-member-functions,misc-multiple-inheritance)
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
                if(is_coroutine_finished()) { destroy_return_value(); }
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
            [[nodiscard]] return_type result()
            {
                ::verilator_utils::check{}(is_coroutine_exited(), "协程尚未执行完成，不能获取结果"sv);
                ::verilator_utils::check{}(is_coroutine_finished(), "协程非正常退出，不能获取结果"sv);
                return get_return_value();
            }
        };

        /**
         * @brief 任务构造函数
         *
         * @param handle 协程句柄
         */
        explicit task(handle_t handle) noexcept : handle_{handle} {}

        /**
         * @brief 任务析构函数，销毁协程句柄
         *
         */
        ~task() noexcept { destroy(); }

        task(const task& other) noexcept = delete;
        task& operator= (const task& other) noexcept = delete;
        task& operator= (task&& other) noexcept = delete;

        task(task&& other) noexcept : handle_{::std::exchange(other.handle_, nullptr)} {}

        /**
         * @brief 检查任务对象是否绑定了协程柄
         *
         * @return 是否绑定了协程柄
         */
        explicit operator bool() const noexcept { return static_cast<bool>(handle_); }

        /**
         * @brief 判断任务对象是否可同步
         *
         * @return 是否可同步
         */
        [[nodiscard]] bool joinable() const noexcept { return static_cast<bool>(handle_); }

        /**
         * @brief 检查任务是否完成
         *
         * @return 任务是否完成
         */
        [[nodiscard]] bool done() const
        {
            ::verilator_utils::check{}(joinable(), "任务未绑定协程，不能检查是否完成"sv);
            return handle_.done();
        }

        /**
         * @brief 恢复任务执行
         *
         */
        void resume()
        {
            ::verilator_utils::check{}(joinable(), "任务未绑定协程，不能恢复执行"sv);
            handle_.resume();
        }

        /**
         * @brief 重新抛出任务中抛出的异常
         *
         * @note 若任务是通过抛出仿真结束异常结束的，则不重新抛出异常
         */
        void rethrow_exception() const { promise().rethrow_exception(); }

        /**
         * @brief 分离任务的协程句柄，此后任务不再持有该句柄
         *
         * @return 任务的协程句柄
         */
        [[nodiscard]] handle_t detach() noexcept { return ::std::exchange(handle_, nullptr); }

        /**
         * @brief 获取任务的协程句柄
         *
         * @return 任务的协程句柄
         */
        [[nodiscard]] handle_t handle() const noexcept { return handle_; }

        /**
         * @brief 获取任务的promise对象
         *
         * @return 任务的promise对象引用
         */
        [[nodiscard]] promise_type& promise() const
        {
            ::verilator_utils::check{}(joinable(), "任务未绑定协程，不能获取承诺体"sv);
            return handle_.promise();
        }

        /**
         * @brief 销毁任务的协程句柄
         *
         */
        void destroy() noexcept
        {
            if(handle_) { ::std::exchange(handle_, nullptr).destroy(); }
        }

        /**
         * @brief 判断任务是否可取消
         *
         * @return 是否可取消
         */
        [[nodiscard]] bool cancel_possible() const
        {
            ::verilator_utils::check{}(joinable(), "任务未绑定协程，不能检查取消状态"sv);
            return handle_.promise().cancel_possible();
        }

        /**
         * @brief 取消任务
         *
         */
        void cancel() const
        {
            ::verilator_utils::check{}(joinable(), "任务未绑定协程，不能取消"sv);
            handle_.promise().cancel();
        }

        /**
         * @brief 判断任务是否收到取消请求
         *
         * @return 是否收到取消请求
         */
        [[nodiscard]] bool cancel_requested() const
        {
            ::verilator_utils::check{}(joinable(), "任务未绑定协程，不能检查取消状态"sv);
            return handle_.promise().cancel_requested();
        }

        /**
         * @brief 调用子任务，立即跳转到子任务执行，等待子任务完成后恢复当前任务执行
         *
         * @return 可等待体
         */
        friend ::verilator_utils::detail::subtask_awaiter<promise_type> operator co_await(const task& subtask)
        {
            ::verilator_utils::check{}(subtask.joinable(), "子任务未绑定协程，不能等待"sv);
            return ::verilator_utils::detail::subtask_awaiter<promise_type>{subtask.handle_};
        }

    private:
        handle_t handle_;
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
     * @brief 带有挂起队列的设施的基类
     *
     */
    struct with_suspend_queue
    {
    };
}  // namespace verilator_utils

namespace verilator_utils::detail
{
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
            ::verilator_utils::check{}(event_callback != nullptr, "事件回调不能为空"sv);
            return (*event_callback)();
        }
    };

    /// 事件队列类型
    using event_queue_t = ::std::vector<::verilator_utils::detail::event_queue_element>;
    /// 就绪队列类型
    using ready_queue_t = ::std::vector<::verilator_utils::detail::coroutine_pair>;

    /// 挂起项类型
    struct suspend_entry
    {
        /// 事件指针
        ::verilator_utils::with_suspend_queue* entry;
        /// 回调函数类型
        using callback_t = void (*)(::verilator_utils::with_suspend_queue&) noexcept;
        /// 释放函数，用于在仿真结束时把挂起在事件上的任务放回调度器
        callback_t release;

        friend bool operator== (const suspend_entry& lhs, const suspend_entry& rhs) noexcept { return lhs.entry == rhs.entry; }

        friend ::std::strong_ordering operator<=> (const suspend_entry& lhs, const suspend_entry& rhs) noexcept
        { return lhs.entry <=> rhs.entry; }

        void operator() () const noexcept { release(*entry); }
    };

    /// 挂起项队列类型
    using suspend_entry_queue_t = ::std::flat_set<suspend_entry>;

    constexpr ::std::array time_unit_table{
        ::std::tuple{0,   1'000'000'000'000'000zu, "s"sv },
        ::std::tuple{-3,  1'000'000'000'000zu,     "ms"sv},
        ::std::tuple{-6,  1'000'000'000zu,         "us"sv},
        ::std::tuple{-9,  1'000'000zu,             "ns"sv},
        ::std::tuple{-12, 1'000zu,                 "ps"sv},
        ::std::tuple{-15, 1zu,                     "fs"sv},
    };
}  // namespace verilator_utils::detail

namespace verilator_utils
{
    export struct eval_scheduler
    {
        /**
         * @brief 评估阶段枚举
         *
         */
        enum class eval_stage_enum : ::std::uint8_t
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
            invalid
        };

    private:
        /// 指向VerilatedModel的指针，由dut类型擦除得到
        ::VerilatedModel& dut_;
        /// dut状态计算函数指针类型
        using dut_eval_t = void (*)(::VerilatedModel&);
        /// dut状态计算函数
        dut_eval_t dut_eval_;
        /// 时间精度，单位为飞秒
        ::std::size_t time_precision_fs_;
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
        /// 挂起项队列
        ::verilator_utils::detail::suspend_entry_queue_t suspend_entry_queue{};
        /// 完成项，用于暂存由子协程链式唤醒并执行完的根协程
        ::verilator_utils::detail::coroutine_pair finish_entry{};

        /// 评估阶段
        eval_stage_enum eval_stage_{eval_stage_enum::not_begin};

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
                dut_.contextp()->time(target_time);
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
         * @brief 处理完成的根协程，销毁协程并进行异常传播
         *
         * @param pair 协程状态对
         */
        void handle_finish_coroutine(::verilator_utils::detail::coroutine_pair pair)
        {
            // 存在根协程->子协程->根协程的链式唤醒路径
            // 因此总是要清空finish_entry
            finish_entry = {};
            // 利用raii确保在异常时销毁handle
            constexpr static auto deleter{[](const ::std::coroutine_handle<>* handle) static noexcept { handle->destroy(); }};
            const auto [handle, promise]{pair};
            const ::std::unique_ptr<const ::std::coroutine_handle<>, decltype(deleter)> _{&handle};
            promise->rethrow_exception();
        }

        /**
         * @brief 评估就绪队列，然后清理完成项
         *
         */
        bool ready_queue_eval()
        {
            const bool any_coroutine_run{!ready_queue.empty()};
            auto i{0zu};
            try
            {
                for(; i != ready_queue.size(); ++i)
                {
                    const auto pair{ready_queue[i]};
                    const auto& [handle, promise]{pair};
                    const auto is_root{promise->parent == nullptr};
                    handle.resume();
                    // 总是积极地进行异常传播，避免调度器在错误状态下继续运行导致数据结构损坏
                    if(is_root)
                    {
                        // 协程为根协程时执行销毁和异常传播
                        if(handle.done()) { handle_finish_coroutine(pair); }
                    }
                    else
                    {
                        // 若子协程的执行导致根协程完成，也对根协程进行销毁和异常传播
                        if(finish_entry != nullptr) { handle_finish_coroutine(finish_entry); }
                    }
                }
            }
            catch(...)
            {
                const auto begin{ready_queue.begin()};
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
         * @param dut 待测模型对象引用
         * @param dut_eval 回调函数，实现dut状态计算
         * @note 调度器会缓存time precision和time unit，因此在构造时需要确保二者已经设置
         */
        explicit eval_scheduler(::VerilatedModel& dut, dut_eval_t dut_eval) noexcept : dut_{dut}, dut_eval_{dut_eval}
        {
            // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer)
            const auto& context{*dut.contextp()};
            const auto time_precision{context.timeprecision()};
            const auto time_unit{context.timeunit()};
            time_precision_fs_ = static_cast<::std::uint64_t>(::std::pow(10, 15 + time_precision));
            time_precision_per_time_unit = ::std::pow(10, time_unit - time_precision);
            for(const auto& [unit_exponent, unit_fs, unit_suffix]: ::verilator_utils::detail::time_unit_table)
            {
                if(time_unit >= unit_exponent)
                {
                    output_unit_per_time_precision = static_cast<double>(time_precision_fs_) / static_cast<double>(unit_fs);
                    output_unit_suffix = unit_suffix;
                    return;
                }
            }
            ::std::unreachable();
            // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
        }

        /**
         * @brief 构造调度器对象
         *
         * @tparam dut_t 待测模型类型，必须派生自VerilatedModel
         * @param dut 指向待测模型对象的指针
         * @note 调度器会缓存time precision和time unit，因此在构造时需要确保二者已经设置
         */
        template <::std::derived_from<::VerilatedModel> dut_t>
        explicit eval_scheduler(dut_t& dut) noexcept :
            eval_scheduler{dut, [](::VerilatedModel& dut) { static_cast<dut_t&>(dut).eval(); }}
        {
        }

        eval_scheduler(const eval_scheduler&) = delete;
        eval_scheduler& operator= (const eval_scheduler&) = delete;
        // 任务会持有调度器指针，移动调度器会导致指针失效
        eval_scheduler(eval_scheduler&&) noexcept = delete;
        eval_scheduler& operator= (eval_scheduler&&) noexcept = delete;

        /**
         * @brief 检查直接由调度器管理的队列是否为空
         *
         * 不考虑在调度器外挂起的协程
         * @return 队列是否为空
         */
        [[nodiscard]] bool empty() const noexcept { return wait_queue.empty() && event_queue.empty() && ready_queue.empty(); }

        /**
         * @brief 检查仿真是否结束
         *
         * @return 仿真是否结束
         */
        [[nodiscard]] bool is_finish() const noexcept { return dut_.contextp()->gotFinish(); }

        /**
         * @brief 检查仿真是否存在错误
         *
         * @return 仿真是否存在错误
         */
        [[nodiscard]] bool is_error() const noexcept { return dut_.contextp()->gotError(); }

        /**
         * @brief 标记仿真结束
         *
         */
        void finish() noexcept { dut_.contextp()->gotFinish(true); }

        /**
         * @brief 标记仿真中出现错误
         *
         */
        void error() noexcept { dut_.contextp()->gotError(true); }

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
        dut_t& dut() const noexcept
        { return *static_cast<dut_t*>(dut_); }

        /**
         * @brief 获取当前时间，单位为dut时间精度
         *
         * @return 当前时间
         */
        [[nodiscard]] ::std::uint64_t time_in_time_precision() const noexcept { return dut_.contextp()->time(); }

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
        [[nodiscard]] ::std::uint64_t time_precision_fs() const noexcept { return time_precision_fs_; }

        /**
         * @brief 获取当前时间，已根据时间单位转换为字符串格式并添加时间单位后缀
         *
         * @return 当前时间的字符串表示
         */
        [[nodiscard]] ::std::string time_in_string() const
        {
            auto time_in_output_unit{static_cast<double>(time_in_time_precision()) * output_unit_per_time_precision};
            return ::std::format("{:.6g}{}"sv, time_in_output_unit, output_unit_suffix);
        }

        /**
         * @brief 析构调度器对象
         *
         */
        ~eval_scheduler() noexcept
        {
            finish();

            while(!wait_queue.empty())
            {
                register_ready(wait_queue.top().pair);
                wait_queue.pop();
            }

            for(const auto& [_, pair]: event_queue) { register_ready(pair); }
            event_queue.clear();

            // 在恢复协程执行前先清理挂起项队列
            // 清理后，同步体上应当不存在挂起的任务，可以安全地析构同步体
            for(const auto& entry: suspend_entry_queue) { entry(); }
            suspend_entry_queue.clear();

            while(!ready_queue.empty())
            {
                try
                {
                    ready_queue_eval();
                }
                catch(...)  // NOLINT(bugprone-empty-catch)
                {
                    // 协作式取消时产生的异常无法进行传播
                }
            }
        }

        /**
         * @brief 获取调度器当前评估阶段
         *
         * @return eval_stage_enum 评估阶段枚举
         */
        [[nodiscard]] eval_stage_enum eval_stage() const noexcept { return eval_stage_; }

        /**
         * @brief 执行一轮评估
         *
         */
        void loop_once()
        {
            using enum eval_stage_enum;

            eval_stage_ = eval_ready_task;
            // 执行已就绪协程
            while(ready_queue_eval()) {}

            // 推进时间步，执行新的就绪协程
            wait_queue_eval();
            eval_stage_ = before_dut_eval;
            while(ready_queue_eval()) {}
            // 循环评估事件队列和就绪队列，直到收敛
            while(event_queue_eval()) { ready_queue_eval(); }

            // 评估电路，该步骤只评估verilator模型，不进行协程调度
            eval_stage_ = on_dut_eval;
            dut_eval_(dut_);

            // 循环评估事件队列和就绪队列，直到收敛
            eval_stage_ = after_dut_eval;
            while(event_queue_eval()) { ready_queue_eval(); }

            // 结束一轮评估
            eval_stage_ = eval_end;
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
            using enum eval_stage_enum;
            ::verilator_utils::check{}(eval_stage_ <= after_initial_eval, "已进入仿真循环阶段，不能执行初始化"sv);
            ::verilator_utils::check{}(eval_stage_ == not_begin, "已执行过initial_eval，不应再次执行"sv);
            while(ready_queue_eval()) {}
            eval_stage_ = after_initial_eval;
        }

        /**
         * @brief 向事件队列中注册事件
         *
         * @param callback 事件回调函数
         * @param pair 协程状态对
         */
        void register_event(::verilator_utils::default_event_callback& callback, ::verilator_utils::detail::coroutine_pair pair)
        { event_queue.emplace_back(&callback, pair); }

        /**
         * @brief 向等待队列中注册等待时间
         *
         * @note 不支持delta延迟，等待时间不能为0
         * @param time_to_wait 等待时间，单位为飞秒，不能为0
         * @param pair 协程状态对
         */
        void register_wait(::verilator_utils::femtosecond_t time_to_wait, ::verilator_utils::detail::coroutine_pair pair)
        {
            ::verilator_utils::check{}(time_to_wait != 0_fs, "不支持delta延迟，等待时间不能为0"sv);
            const auto time_to_wait_in_time_precision{time_to_wait.rep / time_precision_fs_};
            ::verilator_utils::check{}(time_to_wait_in_time_precision != 0, "等待时长小于时间精度，被截断为0"sv);
            const auto current_time{dut_.contextp()->time()};
            const auto target_time{time_to_wait_in_time_precision + current_time};
            ::verilator_utils::check{}(target_time > current_time, "Verilator仿真计时器溢出"sv);
            wait_queue.emplace(target_time, pair);
        }

        /**
         * @brief 向就绪队列中注册协程
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
         * @brief 向完成项中注册协程
         *
         * @param pair 协程状态对
         * @note 只能注册根协程
         */
        void register_finish(::verilator_utils::detail::coroutine_pair pair)
        {
            ::verilator_utils::check{}(pair.promise->parent == nullptr, "完成项对只能注册根协程"sv);
            ::verilator_utils::check{}(finish_entry.handle == nullptr, "完成项不为空"sv);
            finish_entry = pair;
        }

        /**
         * @brief 向挂起项队列中注册协程
         *
         * @param entry 带有挂起队列的对象
         * @param release 回调函数，用于在仿真结束时把挂起队列中的任务交回调度器
         */
        void register_suspend_entry(::verilator_utils::with_suspend_queue& entry,
                                    ::verilator_utils::detail::suspend_entry::callback_t release)
        { suspend_entry_queue.emplace(::std::addressof(entry), release); }

        /**
         * @brief 从挂起项队列中移除协程
         *
         * 若entry不存在则不执行任何动作，析构函数总可以调用该函数而无需检查调度器是否已经清理了挂起项队列
         * @param entry 带有挂起队列的对象
         */
        void remove_suspend_entry(::verilator_utils::with_suspend_queue& entry) noexcept
        { suspend_entry_queue.erase({::std::addressof(entry), nullptr}); }

        /**
         * @brief 向调度器中添加任务
         *
         * @param task 要添加的任务
         */
        void add_task(::verilator_utils::task<void> task)
        {
            // 向task中添加调度器
            task.promise().scheduler = this;
            register_ready(task.detach());
        }
    };

    void resume_coroutine(::verilator_utils::detail::promise_base& promise)
    {
        using enum ::verilator_utils::detail::status_enum;
        // 不允许未绑定调度器的任务
        // NOLINTNEXTLINE(clang-analyzer-core.UndefinedBinaryOperatorResult)
        if(promise.scheduler == nullptr) [[unlikely]] { ::std::unreachable(); }
        // 协作式取消的优先级更高
        promise.scheduler->throw_if_finish();
        // 检查是否需要取消
        if(promise.status == cancel_requested) { throw ::verilator_utils::task_cancel_exception{}; }
        promise.status = running;
    }

    template <typename awaiter_t>
    auto ::verilator_utils::detail::awaiter_wrapper<awaiter_t>::await_resume() -> decltype(auto)
    {
        promise.suspend_location = ::std::source_location{};
        ::verilator_utils::resume_coroutine(promise);
        return awaiter.await_resume();
    }

    void ::verilator_utils::detail::promise_base::initial_awaiter::await_resume()
    { ::verilator_utils::resume_coroutine(promise); }

    auto ::verilator_utils::detail::promise_base::final_awaiter::await_suspend(::std::coroutine_handle<> handle) const noexcept
        -> ::std::coroutine_handle<>
    {
        if(promise.with_unhandled_exception())
        {
            promise.status = status_enum::aborted;
            try
            {
                promise.rethrow_exception();
            }
            catch(const ::verilator_utils::coroutine_exception&)  // NOLINT(bugprone-empty-catch)
            {
                // 已经加入协程栈回溯信息，不进行处理
            }
            catch(...)
            {
                // 尝试注入协程栈回溯信息
                try
                {
                    promise.exception = ::std::make_exception_ptr(
                        ::verilator_utils::coroutine_exception{promise.exception,
                                                               ::verilator_utils::coroutine_stacktrace{{handle, &promise}}});
                }
                catch(...)
                {
                    promise.exception = ::std::make_exception_ptr(::verilator_utils::coroutine_exception{promise.exception});
                }
            }
        }
        else if(promise.status == status_enum::eval_finish_requested) { promise.status = status_enum::aborted; }
        else if(promise.status == status_enum::cancel_requested) { promise.status = status_enum::canceled; }
        else
        {
            promise.status = status_enum::finished;
        }
        // 无父协程或者为异步协程则不进行回溯
        if(promise.parent == nullptr)
        {
            try
            {
                promise.scheduler->register_finish({handle, &promise});
            }
            catch(...)
            {
                ::std::terminate();
            }
            return ::std::noop_coroutine();
        }
        if(promise.is_async) { return ::std::noop_coroutine(); }
        // 父协程为非根协程直接回溯
        return promise.parent.handle;
    }

    template <typename promise_type>
    auto ::verilator_utils::detail::subtask_awaiter<promise_type>::await_resume() -> return_type
    {
        ::verilator_utils::check{}(subhandle.done(), "子任务尚未完成，不能获取结果"sv);
        subhandle.promise().scheduler->throw_if_finish();
        subhandle.promise().rethrow_exception();  // 已处理aborted
        if(subhandle.promise().status == ::verilator_utils::detail::status_enum::canceled)
        {
            throw ::verilator_utils::subtask_cancel_exception{};  // 处理canceled
        }
        return subhandle.promise().result();  // 处理finished
    }
}  // namespace verilator_utils
