module;
export module verilator_utils:async_task;
import :task;

namespace
{
    using namespace ::std::string_view_literals;
}

export namespace verilator_utils
{
    /**
     * @brief 异步任务类型
     * 由于可能托管给调度器，因此只支持无返回值的任务
     * @note co_await后会释放协程帧以实现积极的内存回收
     */
    struct async_task
    {
        /// 协程柄类型
        using handle_t = ::verilator_utils::task<void>::handle_t;

        /**
         * @brief 将同步任务转化为异步任务，并将任务添加到调度器就绪队列
         *
         * @note 异步任务绑定调度器引用，其生命周期不应长于调度器
         * @note 任务必须处于initial_suspend状态，且不能有父任务
         * @note 任务绑定的调度器必须为空或与父协程的调度器相同
         * @param pair 父协程状态对
         * @param task 同步任务对象
         */
        async_task(::verilator_utils::detail::coroutine_pair pair, ::verilator_utils::task<void> task) : handle_{task.handle()}
        {
            ::verilator_utils::check{}(static_cast<bool>(task), "该任务对象未绑定协程"sv);
            auto&& promise{task.promise()};
            ::verilator_utils::check{}(promise.parent == nullptr, "该任务已经绑定到父任务，不能转化为异步任务"sv);
            ::verilator_utils::check{}(promise.status == ::verilator_utils::task<void>::status_enum::initial_suspend,
                                       "该任务已开始执行，不能转化为异步任务"sv);
            auto* scheduler{pair.promise->check_scheduler()};
            ::verilator_utils::check{}(promise.scheduler == nullptr || promise.scheduler == scheduler,
                                       "任务的调度器({})与父任务的调度器({})不同"sv,
                                       static_cast<const void*>(promise.scheduler),
                                       static_cast<const void*>(scheduler));
            promise.is_async = true;
            promise.parent = pair;
            promise.scheduler = scheduler;
            scheduler->add_task(::std::move(task));
        }

        /**
         * @brief 析构异步任务对象并销毁绑定的子协程柄
         *
         * @note 若协程未完成，则将所有权交给调度器
         */
        ~async_task() noexcept { detach(); }

        async_task(const async_task&) noexcept = delete;
        async_task& operator= (const async_task&) noexcept = delete;
        async_task& operator= (async_task&& other) = delete;

        async_task(async_task&& other) noexcept : handle_{::std::exchange(other.handle_, nullptr)} {}

        /**
         * @brief 获取任务的协程句柄
         *
         * @return 任务的协程句柄
         */
        [[nodiscard]] handle_t handle() const noexcept { return handle_; }

        /**
         * @brief 检查任务对象是否绑定了协程柄
         *
         * @return 是否绑定了协程柄
         */
        explicit operator bool() const noexcept { return static_cast<bool>(handle_); }

        /**
         * @brief 检查任务对象是否可等待
         *
         * @return 是否可等待
         */
        [[nodiscard]] bool joinable() const noexcept { return static_cast<bool>(handle_); }

        /**
         * @brief 获取任务的promise引用
         *
         * @return 任务的promise引用
         */
        [[nodiscard]] ::verilator_utils::task<void>::promise_type& promise() const
        {
            ::verilator_utils::check{}(joinable(), "异步任务未绑定协程，不能获取承诺体"sv);
            return handle_.promise();
        }

        /**
         * @brief 判断任务是否执行完
         *
         * @return 任务是否执行完
         */
        [[nodiscard]] bool done() const
        {
            ::verilator_utils::check{}(joinable(), "异步任务未绑定协程，不能检查是否完成"sv);
            return handle_.done();
        }

        /**
         * @brief 判断任务是否可取消
         *
         * @return 是否可取消
         */
        [[nodiscard]] bool cancel_possible() const
        {
            ::verilator_utils::check{}(joinable(), "异步任务未绑定协程，不能检查取消状态"sv);
            return handle_.promise().cancel_possible();
        }

        /**
         * @brief 取消任务
         *
         */
        void cancel() const
        {
            ::verilator_utils::check{}(joinable(), "异步任务未绑定协程，不能取消"sv);
            handle_.promise().cancel();
        }

        /**
         * @brief 判断任务是否收到取消请求
         *
         * @return 是否收到取消请求
         */
        [[nodiscard]] bool cancel_requested() const
        {
            ::verilator_utils::check{}(joinable(), "异步任务未绑定协程，不能检查取消状态"sv);
            return handle_.promise().cancel_requested();
        }

        /**
         * @brief 分离异步任务的协程柄，此后异步任务不再持有该协程柄
         *
         * @note 若任务已完成则销毁协程柄，若任务未完成则托管给调度器
         * @return 异步任务的协程柄
         */
        void detach() noexcept
        {
            if(handle_)
            {
                if(handle_.done()) { handle_.destroy(); }
                else
                {
                    // 将孤儿协程托管给调度器
                    handle_.promise().is_async = false;
                    handle_.promise().parent = {};
                }
                handle_ = nullptr;
            }
        }

        /**
         * @brief 实现异步子任务的可等待体
         *
         */
        struct async_task_awaiter : ::verilator_utils::detail::with_promise_awaiter
        {
            /// 子任务的协程柄
            handle_t subhandle;
            /// 父任务的承诺指针
            ::verilator_utils::detail::promise_base* promise{};

            void set_promise_impl(::verilator_utils::detail::promise_base& promise) { this->promise = &promise; }

            /**
             * @brief 判断是否立即完成
             *
             * @return 子任务已执行完则立即完成
             */
            [[nodiscard]] bool await_ready() const { return subhandle.done(); }

            /**
             * @brief 向调度器事件队列中注册等待事件，然后挂起协程
             *
             * @param handle 当前任务的协程柄
             * @note 等待关系不成立时无法确定子协程应托管给哪个调度器，且协程柄已脱离任务对象，
             * 无法通过异常处理重新控制子协程，因此以不可恢复断言终止程序
             */
            void await_suspend(handle_t handle) const
            {
                auto&& promise{subhandle.promise()};
                promise.is_async = false;
                ::verilator_utils::check{}(::std::nothrow,
                                           promise.parent == handle,
                                           "等待该异步任务的协程({})与创建任务的协程({})不同",
                                           handle.address(),
                                           promise.parent.handle.address());
            }

            /**
             * @brief 恢复等待任务的执行
             *
             * @note 异步任务下，父子任务同时存在于调度队列中，不能在子任务完成前恢复父任务
             * @throws 若子任务抛出异常则重新抛出异常
             */
            void await_resume() const { ::verilator_utils::detail::check_before_parent_resume(promise, subhandle); }

            async_task_awaiter(const async_task_awaiter&) = delete;
            async_task_awaiter& operator= (const async_task_awaiter&) = delete;
            async_task_awaiter& operator= (async_task_awaiter&&) = delete;

            explicit async_task_awaiter(handle_t subhandle) noexcept : subhandle{subhandle} {}

            async_task_awaiter(async_task_awaiter&& other) noexcept :
                subhandle{::std::exchange(other.subhandle, nullptr)}, promise{other.promise}
            {
            }

            ~async_task_awaiter() noexcept
            {
                // 等待关系在await_suspend处已断言成立，恢复父协程时子协程必然已执行完毕
                if(subhandle != nullptr) { subhandle.destroy(); }
            }
        };

        async_task_awaiter operator co_await()
        {
            ::verilator_utils::check{}(joinable(), "异步任务未绑定协程，不能等待"sv);
            return async_task_awaiter{::std::exchange(handle_, nullptr)};
        }

    private:
        /// 子任务的协程柄
        handle_t handle_;
    };

    /**
     * @brief 异步任务池
     *
     */
    struct spawn_pool
    {
        /**
         * @brief join_all操作抛出的异常类型
         *
         */
        struct join_all_exception : ::std::exception
        {
        private:
            using exceptions_t = ::std::vector<::std::exception_ptr>;
            exceptions_t exceptions_{};
            ::std::string message{};

        public:
            explicit join_all_exception(exceptions_t exceptions) : ::std::exception{}, exceptions_{std::move(exceptions)}
            {
                constexpr static auto message_transform{[](const ::std::exception_ptr& exception_ptr) static {
                    try
                    {
                        ::std::rethrow_exception(exception_ptr);
                    }
                    catch(const ::std::exception& exception)
                    {
                        return ::std::string_view{exception.what()};
                    }
                    catch(...)
                    {
                        return "unknown"sv;
                    }
                }};
                // 逐条拼接消息：使用std::format_to向字符串追加长消息会在libc++的
                // 格式化缓冲区中触发硬化解言，因此这里不经过格式化器
                for(auto [i, msg]: exceptions_ | ::std::views::transform(message_transform) | ::std::views::enumerate)
                {
                    message += ::std::to_string(i + 1);
                    message += ": ";
                    message += msg;
                    message += '\n';
                }
            }

            [[nodiscard]] const char* what() const noexcept override { return message.c_str(); }

            /**
             * @brief 获取所有的异常集合
             *
             * @return 异常集合引用
             */
            [[nodiscard]] const exceptions_t& exceptions() const noexcept { return exceptions_; }
        };

    private:
        using pool_t = ::std::vector<::verilator_utils::async_task>;
        /// 任务池
        pool_t pool;
        /// 父协程状态对
        ::verilator_utils::detail::coroutine_pair pair;
        /// 同步任务类型
        using task_t = ::verilator_utils::task<void>;

        /**
         * @brief 执行join_all操作
         *
         * @return 子协程
         */
        [[nodiscard]] task_t do_join_all()
        {
            ::std::vector<::std::exception_ptr> exceptions{};
            const auto handle{co_await ::verilator_utils::handle<task_t::promise_type>()};
            // 统一修改父协程
            for(auto&& subtask: pool) { subtask.promise().parent = handle; }
            for(auto&& subtask: pool)
            {
                try
                {
                    co_await subtask;
                }
                catch(...)
                {
                    exceptions.emplace_back(::std::current_exception());
                }
            }
            pool.clear();
            if(!exceptions.empty()) { throw join_all_exception{::std::move(exceptions)}; }
        }

        /**
         * @brief 实现等待任务池中任意任务完成使用的可等待体
         *
         */
        struct join_any_awaiter : ::verilator_utils::detail::with_promise_awaiter
        {
            /// 子任务视图
            pool_t* pool;
            /// 首个完成任务的指针
            ::verilator_utils::async_task* ptr{};
            /// 父任务的承诺指针
            ::verilator_utils::detail::promise_base* promise{};

            explicit join_any_awaiter(pool_t& pool) noexcept : pool{&pool} {}

            void set_promise_impl(::verilator_utils::detail::promise_base& promise) { this->promise = &promise; }

            /**
             * @brief 判断是否立即完成
             *
             * @return 所有子任务都已执行完则立即完成
             */
            bool await_ready() { return any_tasks_done(); }

            /**
             * @brief 将子协程暂时转化为同步协程，以便由子协程唤醒父协程
             *
             * @param handle 当前任务的协程柄
             * @note 等待关系不成立时子协程会唤醒创建任务的协程而不是当前协程，当前协程将永远不被恢复；
             * 且协程柄已脱离任务对象，无法通过异常处理重新控制子协程，因此以不可恢复断言终止程序
             */
            void await_suspend(::verilator_utils::async_task::handle_t handle) const
            {
                const auto parent{pool->begin()->promise().parent};
                ::verilator_utils::check{}(::std::nothrow,
                                           handle == parent,
                                           "等待该异步任务的协程({})与创建任务的协程({})不同",
                                           handle.address(),
                                           parent.handle.address());
                for(auto&& subtask: *pool) { subtask.promise().is_async = false; }
            }

            /**
             * @brief 恢复等待任务的执行
             *
             */
            void await_resume()
            {
                search_finish_task();
                ::verilator_utils::detail::check_before_parent_resume(promise, ptr->handle());
            }

            join_any_awaiter(const join_any_awaiter&) noexcept = delete;
            join_any_awaiter& operator= (const join_any_awaiter&) noexcept = delete;
            join_any_awaiter& operator= (join_any_awaiter&&) noexcept = delete;

            join_any_awaiter(join_any_awaiter&& other) noexcept :
                pool{::std::exchange(other.pool, nullptr)}, ptr{other.ptr}, promise{other.promise}
            {
            }

            ~join_any_awaiter() noexcept
            {
                if(pool == nullptr) { return; }
                search_finish_task();
                constexpr static auto deleter{
                    [](join_any_awaiter* self) noexcept {
                        // 若待析构的元素不为最后一个元素，则将最后的元素移动到当前位置，然后析构最后的空元素
                        // 否则直接析构元素
                        if(self->ptr != ::std::addressof(self->pool->back()))
                        {
                            ::std::destroy_at(self->ptr);
                            ::std::construct_at(self->ptr, ::std::move(self->pool->back()));
                        }
                        self->pool->pop_back();
                    },
                };
                const ::std::unique_ptr<join_any_awaiter, decltype(deleter)> _{this, deleter};
            }

        private:
            /**
             * @brief 判断是否所有子任务都执行完毕
             *
             * @return 是否所有子任务都执行完毕
             */
            bool any_tasks_done()
            {
                const auto iter{
                    ::std::ranges::find(*pool, true, [](::verilator_utils::async_task& task) { return task.done(); })};
                if(iter != pool->end()) { ptr = ::std::to_address(iter); }
                return ptr != nullptr;
            }

            /**
             * @brief 搜索就绪的任务
             *
             */
            void search_finish_task() noexcept
            {
                // 没有子任务立即就绪，需要遍历任务池查找就绪任务
                if(ptr == nullptr)
                {
                    for(auto&& subtask: *pool)
                    {
                        // 在task进入pool时已经进行过检查
                        // 使用无检查的get_handle()以避免重复的断言
                        subtask.handle().promise().is_async = true;
                        if(subtask.handle().done()) { ptr = ::std::addressof(subtask); }
                    }

                    // 标记ptr为空的情况不可达以消除静态分析警告
                    if(ptr == nullptr) [[unlikely]] { ::std::unreachable(); }
                }
            }
        };

    public:
        /**
         * @brief 构造异步任务池
         *
         * @param pair 父协程状态对
         */
        explicit spawn_pool(::verilator_utils::detail::coroutine_pair pair) noexcept : pool{}, pair{pair} {}

        spawn_pool(const spawn_pool&) = delete;
        spawn_pool& operator= (const spawn_pool&) = delete;
        spawn_pool& operator= (spawn_pool&&) = delete;
        spawn_pool(spawn_pool&& other) noexcept = default;
        ~spawn_pool() noexcept = default;

        /**
         * @brief 将同步任务转化为异步任务并添加到任务池中
         *
         * @param task 同步任务
         */
        void add_task(task_t task)
        {
            ::verilator_utils::check{}(task.joinable(), "任务未绑定协程，不能转化为异步任务"sv);
            pool.emplace_back(pair, ::std::move(task));
        }

        /**
         * @brief 判断任务池是否为空
         *
         * @return 任务池是否为空
         */
        [[nodiscard]] bool empty() const { return pool.empty(); };

        /**
         * @brief 判断任务池是否可等待
         *
         * @return 任务池是否可等待
         */
        [[nodiscard]] bool joinable() const { return !pool.empty(); };

        /**
         * @brief 等待任务池中所有任务完成
         *
         * @return 子协程，在其上执行co_await以获取结果
         */
        [[nodiscard]] task_t join_all()
        {
            ::verilator_utils::check{}(joinable(), "任务集合不能为空"sv);
            return do_join_all();
        }

        /**
         * @brief 等待任务池中任意任务完成
         *
         * @return 可等待体
         */
        [[nodiscard]] join_any_awaiter join_any()
        {
            ::verilator_utils::check{}(joinable(), "任务集合不能为空"sv);
            return join_any_awaiter{pool};
        }

        /**
         * @brief 将池中所有任务托管给调度器
         *
         * @return 可等待体
         */
        [[nodiscard]] ::std::suspend_never join_none()
        {
            ::verilator_utils::check{}(joinable(), "任务集合不能为空"sv);
            pool.clear();
            return ::std::suspend_never{};
        }
    };
}  // namespace verilator_utils

namespace verilator_utils::detail
{
    /**
     * @brief 实现无挂起异步任务池获取的可等待体
     *
     */
    struct spawn_pool_awaiter : ::verilator_utils::detail::no_suspend_awaiter
    {
        /// 协程状态对
        ::verilator_utils::detail::coroutine_pair pair;

        template <::verilator_utils::is_coroutine_promise promise_type>
        void set_promise_impl(promise_type& promise) noexcept
        { pair = promise; }

        [[nodiscard]] ::verilator_utils::spawn_pool await_resume() const { return ::verilator_utils::spawn_pool{pair}; }
    };

    /**
     * @brief 实现同步任务转异步任务的可等待体
     *
     */
    struct to_async_awaiter : ::verilator_utils::detail::no_suspend_awaiter
    {
        /// 同步任务
        ::verilator_utils::task<void> task;
        /// 协程状态对
        ::verilator_utils::detail::coroutine_pair pair{};

        template <::verilator_utils::is_coroutine_promise promise_type>
        void set_promise_impl(promise_type& promise) noexcept
        { pair = promise; }

        [[nodiscard]] ::verilator_utils::async_task await_resume()
        { return ::verilator_utils::async_task{pair, ::std::move(task)}; }
    };

    struct add_task_awaiter : ::verilator_utils::detail::no_suspend_awaiter
    {
        /// 同步任务
        ::verilator_utils::task<void> task;
        /// 调度器指针
        ::verilator_utils::eval_scheduler* scheduler{};

        void set_promise_impl(::verilator_utils::detail::promise_base& promise) { scheduler = promise.check_scheduler(); }

        void await_resume() { scheduler->add_task(::std::move(task)); }
    };
}  // namespace verilator_utils::detail

export namespace verilator_utils
{
    /**
     * @brief 获取异步任务池
     *
     * @return 可等待体
     * @code {.cpp}
     * task foo()
     * {
     *     auto pool{co_await spawn_pool()};
     * }
     * @endcode
     */
    [[nodiscard]] ::verilator_utils::detail::spawn_pool_awaiter spawn_pool() noexcept
    { return ::verilator_utils::detail::spawn_pool_awaiter{}; }

    /**
     * @brief 将同步任务转为异步任务
     *
     * @param task 同步任务
     * @return 可等待体
     */
    [[nodiscard]] ::verilator_utils::detail::to_async_awaiter to_async(::verilator_utils::task<void> task)
    { return ::verilator_utils::detail::to_async_awaiter{.task = ::std::move(task)}; }

    /**
     * @brief 向调度器中添加一个任务
     *
     * @param task 同步任务
     * @return 可等待体
     */
    [[nodiscard]] ::verilator_utils::detail::add_task_awaiter add_task(::verilator_utils::task<void> task)
    { return ::verilator_utils::detail::add_task_awaiter{.task = ::std::move(task)}; }
}  // namespace verilator_utils
