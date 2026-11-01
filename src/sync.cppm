module;
export module verilator_utils:sync;
import :task;

namespace
{
    using namespace ::std::string_view_literals;
}

export namespace verilator_utils
{
    /**
     * @brief 边沿触发的事件，用于协程间同步，类似SystemVerilog event
     *
     */
    struct event : ::verilator_utils::with_suspend_queue  // NOLINT(cppcoreguidelines-virtual-class-destructor)
    {
    private:
        using pair_t = ::verilator_utils::detail::coroutine_pair;
        /// 挂起队列类型，考虑测试激励中协程的数量不会很多，使用vector而不是deqeue
        using suspend_queue_t = ::std::vector<pair_t>;
        /// 挂起队列
        suspend_queue_t suspend_queue{};
        ::verilator_utils::eval_scheduler* scheduler{};
        // 延迟回收以接近平均O(1)的复杂度
        constexpr static auto erase_watermark{4096zu / sizeof(pair_t)};
        constexpr static auto shrink_watermark{erase_watermark * 4zu};
        ::std::size_t head_index{};

        /**
         * @brief 实现挂起功能的可等待体
         *
         */
        struct suspend_awaiter : ::std::suspend_always
        {
            event& self;

            template <::verilator_utils::is_coroutine_promise promise_type>
            void await_suspend(::std::coroutine_handle<promise_type> handle)
            {
                auto new_scheduler{handle.promise().check_scheduler()};
                if(self.scheduler == nullptr)
                {
                    self.scheduler = new_scheduler;
                    self.scheduler->register_suspend_entry(self);
                }
                else
                {
                    ::verilator_utils::check{}(self.scheduler == new_scheduler,
                                               "等待同一event对象的协程必须绑定相同的调度器对象"sv);
                }
                self.suspend_queue.emplace_back(handle);
            }
        };

        /**
         * @brief 在空闲容量超出阈值时回收内存
         *
         */
        void do_shrink()
        {
            if(suspend_queue.capacity() - suspend_queue.size() >= shrink_watermark) [[unlikely]]
            {
                suspend_queue.shrink_to_fit();
            }
        }

        void do_notify_all() noexcept
        {
            ::std::ranges::for_each(suspend_queue | ::std::views::drop(static_cast<::std::ptrdiff_t>(head_index)),
                                    [scheduler = this->scheduler](const pair_t& pair) { scheduler->register_ready(pair); });
            suspend_queue.clear();
            head_index = 0;
        }

        [[nodiscard]] bool empty() const noexcept { return suspend_queue.size() == head_index; }

    protected:
        /**
         * @brief 调度器析构时回收挂起队列并解除与调度器的绑定
         */
        void release() noexcept override
        {
            do_notify_all();
            // 调度器即将析构，同步体无需再向调度器注销挂起队列
            scheduler = nullptr;
        }

    public:
        /**
         * @brief 构造事件对象
         *
         * @param location 事件对象的构造点，默认取调用处的源代码位置
         */
        constexpr explicit event(::std::source_location location = ::std::source_location::current()) noexcept :
            ::verilator_utils::with_suspend_queue{location}
        {
        }

        event(const event&) noexcept = delete;
        event& operator= (const event&) noexcept = delete;
        event(event&&) noexcept = delete;
        event& operator= (event&&) noexcept = delete;

        ~event() noexcept
        {
            if(scheduler != nullptr)
            {
                scheduler->remove_suspend_entry(*this);
                ::verilator_utils::check{}(::std::nothrow,
                                           scheduler->is_finish() || empty(),
                                           "在存在挂起任务时析构同步体是未定义行为"sv);
                // 对于仿真完成的情况，任务行为有良好的定义，一唤醒就会执行退出流程
                if(scheduler->is_finish()) { do_notify_all(); }
            }
            else
            {
                // 调度器先析构或挂起队列已注销时，同步体上不应再有挂起的任务
                ::verilator_utils::check{}(::std::nothrow, empty(), "在存在挂起任务时析构同步体是未定义行为"sv);
            }
        }

#ifndef __cpp_rtti
        /**
         * @brief 获取事件类型名称
         *
         * RTTI不可用时无法通过typeid获取动态类型，直接返回固定名称
         * @return 类型名称
         */
        [[nodiscard]] ::std::string dump_type() const override
        { return ::std::string{"verilator_utils::event@verilator_utils"sv}; }
#endif

        /**
         * @brief 判断事件挂起队列是否为空
         *
         * @return 挂起队列是否为空
         */
        [[nodiscard]] bool dump_empty() const noexcept override { return empty(); }

        /**
         * @brief 获取所有挂起的协程状态对
         *
         * 已被通知但尚未从挂起队列中回收的协程不计入结果
         * @return 挂起协程状态对数组
         */
        [[nodiscard]] ::std::vector<pair_t> dump_entries() const override
        { return suspend_queue | ::std::views::drop(head_index) | ::std::ranges::to<::std::vector>(); }

        /**
         * @brief 唤醒所有等待此事件的协程
         *
         */
        void notify_all()
        {
            if(head_index < suspend_queue.size())
            {
                ::verilator_utils::check{}(scheduler != nullptr, "event未绑定调度器，但等待队列不为空"sv);
                do_notify_all();
                do_shrink();
            }
        }

        /**
         * @brief 按FIFO顺序唤醒一个等待此事件的协程
         *
         * 可避免惊群效应
         */
        void notify_one()
        {
            if(head_index < suspend_queue.size())
            {
                ::verilator_utils::check{}(scheduler != nullptr, "event未绑定调度器，但等待队列不为空"sv);
                auto iter{suspend_queue.begin() + static_cast<::std::ptrdiff_t>(head_index++)};
                scheduler->register_ready(*iter);
                if(head_index == erase_watermark) [[unlikely]]
                {
                    suspend_queue.erase(suspend_queue.begin(), ++iter);
                    head_index = 0;
                    do_shrink();
                }
            }
        }

        /**
         * @brief 回收等待队列中的空闲空间
         *
         */
        void shrink_to_fit()
        {
            const auto iter{suspend_queue.begin() + static_cast<::std::ptrdiff_t>(head_index)};
            suspend_queue.erase(suspend_queue.begin(), iter);
            head_index = 0;
            suspend_queue.shrink_to_fit();
        }

        /**
         * @brief 等待直到事件被触发
         *
         * @param self 事件对象
         * @return 可等待体
         */
        [[nodiscard]] friend suspend_awaiter operator co_await(event& self) noexcept { return suspend_awaiter{.self{self}}; }
    };

    /**
     * @brief 邮箱，类似SystemVerilog mailbox
     *
     * @tparam type 元素类型
     */
    template <::std::move_constructible type>
    struct mailbox
    {
        using value_type = type;
        using reference = value_type&;
        using const_reference = const value_type&;

    private:
        friend struct ::std::formatter<mailbox>;
        ::std::size_t max_count{};
        using wrapper = ::std::optional<type>;
        ::std::vector<wrapper> buffer{};
        // put函数在该事件上等待
        ::verilator_utils::event write_event{};
        // get和peek函数在该事件上等待
        ::verilator_utils::event read_event{};
        // 延迟回收以接近平均O(1)的复杂度
        constexpr static auto erase_watermark{::std::max(4096zu / sizeof(type), 4zu)};
        constexpr static auto shrink_watermark{erase_watermark * 4zu};
        // 有限容量实现为环形缓冲区，无限容量实现为延迟回收的动态数组
        ::std::size_t head_index{};  // 总是小于max_count
        ::std::size_t tail_index{};  // 可能大于max_count，使用前需要取模

        [[nodiscard]] bool infty_capicity() const noexcept { return max_count == 0; }

        [[nodiscard]] bool full() const noexcept { return head_index + max_count == tail_index; }

        [[nodiscard]] bool empty() const noexcept { return num() == 0; }

        /**
         * @brief 向缓冲区末尾添加一个元素
         *
         * @tparam args_t 参数类型列表
         * @param args 参数列表
         */
        template <typename... args_t>
        void emplace_back(args_t&&... args)
        {
            if(infty_capicity()) { buffer.emplace_back(::std::forward<args_t>(args)...); }
            else
            {
                buffer[tail_index++ % max_count].emplace(::std::forward<args_t>(args)...);
            }
            read_event.notify_one();
        }

        /**
         * @brief 从缓冲区获取首个元素
         *
         * @return 首个元素引用
         */
        const_reference peek_front()
        {
            // peek操作没有真正取走首个元素，因此转发read_event以唤醒其他等待的协程
            read_event.notify_one();
            return buffer[head_index].value();
        }

        /**
         * @brief 从缓冲区获取首个元素，然后删除该元素
         *
         * @return 首个元素
         */
        type get_front()
        {
            auto iter{buffer.begin() + static_cast<::std::ptrdiff_t>(head_index++)};
            type front{::std::move(iter->value())};
            iter->reset();
            if(infty_capicity())
            {
                if(head_index == erase_watermark) [[unlikely]]
                {
                    buffer.erase(buffer.begin(), ++iter);
                    head_index = 0;
                    if(buffer.capacity() - buffer.size() >= shrink_watermark) [[unlikely]] { buffer.shrink_to_fit(); }
                }
            }
            else
            {
                // 对索引进行回绕
                if(head_index == max_count)
                {
                    head_index = 0;
                    tail_index -= max_count;
                }
            }
            write_event.notify_one();
            return front;
        }

        auto as_range_infty_capicity(this auto&& self) { return self.buffer | ::std::views::drop(self.head_index); }

        auto as_range(this auto&& self)
        {
            struct range
            {
                decltype(::std::addressof(self)) ptr;

                struct iterator
                {
                    decltype(::std::addressof(self)) ptr;
                    ::std::size_t index;
                    using difference_type [[maybe_unused]] = ::std::ptrdiff_t;
                    using value_type [[maybe_unused]] = wrapper;

                    iterator& operator++ () noexcept
                    {
                        ++index;
                        return *this;
                    }

                    // 后置自增不会被任何算法调用：范围算法与格式化只使用前置自增
                    // 保留该重载仅为满足std::input_iterator对i++表达式有效的要求
                    iterator operator++ (int) noexcept
                    {
                        auto temp{*this};
                        ++*this;
                        return temp;
                    }

                    auto&& operator* () const noexcept { return ptr->buffer[index % ptr->max_count]; }
                };

                struct sentinel
                {
                    ::std::size_t tail_index;

                    bool operator== (const iterator& iter) const { return tail_index == iter.index; }
                };

                iterator begin() noexcept { return iterator{ptr, ptr->head_index}; }

                sentinel end() noexcept { return sentinel{ptr->tail_index}; }
            };

            return range{::std::addressof(self)};
        }

    public:
        /**
         * @brief 创建邮箱对象
         *
         * @param max_count 邮箱最大容量，为0表示无限容量
         */
        explicit mailbox(::std::size_t max_count = 0) : max_count{max_count}, tail_index{infty_capicity() ? -1zu : 0zu}
        {
            if(max_count != 0) { buffer.resize(max_count); }
        }

        mailbox(const mailbox&) noexcept = delete;
        mailbox& operator= (const mailbox&) noexcept = delete;
        mailbox(mailbox&&) noexcept = delete;
        mailbox& operator= (mailbox&&) noexcept = delete;

        ~mailbox() noexcept
        {
            constexpr static auto do_destroy{[](wrapper& ref) static noexcept { ref.reset(); }};
            if(infty_capicity()) { ::std::ranges::for_each(as_range_infty_capicity(), do_destroy); }
            else
            {
                ::std::ranges::for_each(as_range(), do_destroy);
            }
        }

        /**
         * @brief 获取邮箱内元素数量
         *
         * @return 元素数量
         */
        [[nodiscard]] ::std::size_t num() const noexcept { return (infty_capicity() ? buffer.size() : tail_index) - head_index; }

        /**
         * @brief 向邮箱末尾放入元素，容量不足时会阻塞
         *
         * @tparam args_t 参数类型列表
         * @param args 参数列表
         * @return 子任务，配合co_await使用
         */
        template <typename... args_t>
        [[nodiscard]] ::verilator_utils::task<void> put(args_t&&... args)
            requires (::std::constructible_from<value_type, args_t...>)
        {
            while(full()) { co_await write_event; }
            emplace_back(::std::forward<args_t>(args)...);
        }

        /**
         * @brief 尝试向邮箱末尾放入元素，不会阻塞
         *
         * @tparam args_t 参数类型列表
         * @param args 参数列表
         * @return 是否成功放入元素
         */
        template <typename... args_t>
        [[nodiscard]] bool try_put(args_t&&... args)
            requires (::std::constructible_from<value_type, args_t...>)
        {
            if(full()) { return false; }
            emplace_back(::std::forward<args_t>(args)...);
            return true;
        }

        /**
         * @brief 从邮箱获取首个元素，然后删除该元素，邮箱为空时会阻塞
         *
         * @return 子任务，配合co_await使用
         */
        [[nodiscard]] ::verilator_utils::task<value_type> get()
        {
            while(empty()) { co_await read_event; }
            co_return get_front();
        }

        /**
         * @brief 尝试从邮箱获取首个元素，然后删除该元素，不会阻塞
         *
         * @return std::optional 成功获取时包含元素，否则为空
         */
        [[nodiscard]] ::std::optional<value_type> try_get()
        { return empty() ? ::std::nullopt : ::std::optional<value_type>{get_front()}; }

        /**
         * @brief 从邮箱获取首个元素，不会删除元素，邮箱为空时会阻塞
         *
         * @return 子任务，配合co_await使用
         */
        [[nodiscard]] ::verilator_utils::task<value_type> peek()
        {
            while(empty()) { co_await read_event; }
            co_return peek_front();
        }

        /**
         * @brief 从邮箱获取首个元素，不会删除元素和阻塞
         *
         * @return std::optional 成功获取时包含元素，否则为空
         */
        [[nodiscard]] ::std::optional<value_type> try_peek() const
        { return empty() ? ::std::nullopt : ::std::optional<value_type>{buffer[head_index].value()}; }
    };

    /**
     * @brief 信号量，类似SystemVerilog semaphore
     *
     */
    struct semaphore
    {
        /**
         * @brief 创建具有指定计数器初值的信号量
         *
         * @param initial_count 计数器初值
         */
        explicit semaphore(::std::size_t initial_count = 0) noexcept : count{initial_count} {}

        semaphore(const semaphore&) noexcept = delete;
        semaphore& operator= (const semaphore&) noexcept = delete;
        semaphore(semaphore&&) noexcept = delete;
        semaphore& operator= (semaphore&&) noexcept = delete;
        ~semaphore() noexcept = default;

        /**
         * @brief 增加内部计数器
         *
         * @param update 要增加的量
         */
        void put(::std::size_t update = 1)
        {
            count += update;
            while(!empty() && count >= suspend_queue[head_index])
            {
                const auto begin{suspend_queue.begin()};
                const auto iter{begin + static_cast<::std::ptrdiff_t>(head_index)};
                event.notify_one();
                // 在此处更新内部计数器使得结果立即对外部可见，避免虚假唤醒
                count -= *iter;
                if(++head_index == erase_watermark) [[unlikely]]
                {
                    head_index = 0;
                    suspend_queue.erase(begin, iter + 1);
                    if(suspend_queue.capacity() - suspend_queue.size() >= shrink_watermark) [[unlikely]]
                    {
                        suspend_queue.shrink_to_fit();
                    }
                }
            }
        }

        /**
         * @brief 减少内部计数器，阻塞直到能如此
         *
         * @param update 要减小的量
         * @return 子任务，配合co_await使用
         */
        [[nodiscard]] ::verilator_utils::task<void> get(::std::size_t update = 1)
        {
            if(count >= update && empty())
            {
                count -= update;
                co_return;
            }
            suspend_queue.emplace_back(update);
            // 同步体的生命周期不长于调度器时，仿真结束后同步体可能已经析构，
            // 因此先取得调度器（协程运行期间它必然存活）以判断能否访问自身状态
            const auto& scheduler{co_await ::verilator_utils::scheduler()};
            try
            {
                co_await event;
            }
            catch(...)
            {
                // 在抛出异常时清理suspend_queue以避免破坏和event队列的对齐；
                // 仿真结束后的等待者只执行退出流程，此时挂起队列的对齐不再有意义
                if(!scheduler.is_finish()) { suspend_queue.pop_back(); }
                throw;
            }
        }

        /**
         * @brief 尝试减少内部计数器，不会阻塞
         *
         * @param update 要减小的量
         * @return 是否成功减小计数器
         */
        bool try_get(::std::size_t update = 1) noexcept
        {
            if(count >= update && empty())
            {
                count -= update;
                return true;
            }
            return false;
        }

    private:
        ::std::size_t count{};
        constexpr static auto erase_watermark{4096zu / sizeof(::std::size_t)};
        constexpr static auto shrink_watermark{erase_watermark * 4zu};
        ::std::vector<::std::size_t> suspend_queue{};
        ::std::size_t head_index{};
        ::verilator_utils::event event{};

        bool empty() noexcept { return suspend_queue.size() == head_index; }
    };

    /**
     * @brief 移位寄存器，在参考模型中对信号进行延迟
     *
     * @tparam type 数据类型
     */
    template <::verilator_utils::is_format_wrapper_data_type type>
    struct shift_register
    {

        /**
         * @brief 初始化移位寄存器
         *
         * @param depth 寄存器链深度
         * @param packed_format 打包的数据格式
         */
        explicit shift_register(::std::size_t depth, ::verilator_utils::packed_format packed_format) :
            depth{depth}, format{packed_format}
        { reg.reserve(depth); }

        /**
         * @brief 初始化移位寄存器
         *
         * @param depth 寄存器链深度
         * @param width 数据宽度
         * @param format 数据格式
         */
        explicit shift_register(::std::size_t depth, ::size_t width, const ::verilator_utils::data_format::format& format) :
            shift_register{
                depth,
                {width, format}
        }
        {
        }

    private:
        // 寄存器链深度
        ::std::size_t depth;
        // 打包储存的格式
        ::verilator_utils::packed_format format;
        // 寄存器链
        ::std::vector<type> reg{};
        // 缓冲最近移出寄存器链的元素，实现enable功能
        ::std::optional<type> buffer{};
        using arg_t = ::std::conditional_t<(sizeof(type) > sizeof(::std::size_t)), const type&, type>;
        friend struct ::std::formatter<shift_register>;
        using wrapper_t = ::verilator_utils::format_wrapper<type>;
        using optional_t = ::std::optional<wrapper_t>;

    public:
        /**
         * @brief 更新移位寄存器中的值
         *
         * @param value 要更新的值
         * @param enable 使能信号，为false时只读取寄存器链，为true时进行更新
         * @return 最早进入寄存器链的值，若寄存器链未填满则为空
         */
        optional_t update(arg_t value, bool enable = true)
        {
            if(enable)
            {
                if(depth == 0) { buffer.emplace(value); }
                else
                {
                    if(reg.size() == depth)
                    {
                        buffer.emplace(reg.front());
                        reg.erase(reg.begin());
                    }
                    reg.emplace_back(value);
                }
            }
            return buffer.transform([this](arg_t buffered_value) { return wrapper_t{buffered_value, format}; });
        }

        /**
         * @brief 清空寄存器链
         *
         */
        void reset() noexcept
        {
            reg.clear();
            buffer.reset();
        }
    };
}  // namespace verilator_utils

export namespace std
{
    /**
     * @brief 邮箱格式化支持
     *
     * 支持的格式符：
     * - #: 输出详细信息
     * - {}: 由嵌套参数动态选择是否输出详细信息，参数类型须为bool
     * @tparam type 元素类型
     */
    template <::std::move_constructible type>
    struct formatter<::verilator_utils::mailbox<type>>
    {
        bool with_detail{};
        ::std::size_t detail_argument_id{::verilator_utils::detail::format_string::npos};

        constexpr auto parse(::std::format_parse_context& ctx)
        {
            return ::verilator_utils::detail::format_string::parse_detail_flag(ctx,
                                                                               "无效的verilator_utils::mailbox格式符"sv,
                                                                               with_detail,
                                                                               detail_argument_id);
        }

        template <typename iter_t>
        auto format(const ::verilator_utils::mailbox<type>& value, ::std::basic_format_context<iter_t, char>& ctx) const
        {
            constexpr static auto transform{::std::views::transform([](const auto& ref) static noexcept { return ref.value(); })};
            if(::verilator_utils::detail::format_string::resolve_detail_flag(ctx, with_detail, detail_argument_id))
            {
                if(value.infty_capicity())
                {
                    return ::std::format_to(ctx.out(),
                                            "{{max_count: {}, value: {}}}"sv,
                                            value.max_count,
                                            value.as_range_infty_capicity() | transform);
                }
                return ::std::format_to(ctx.out(),
                                        "{{max_count: {}, value: {}}}"sv,
                                        value.max_count,
                                        value.as_range() | transform);
            }

            if(value.infty_capicity())
            {
                return ::std::format_to(ctx.out(), "{}"sv, value.as_range_infty_capicity() | transform);
            }
            return ::std::format_to(ctx.out(), "{}"sv, value.as_range() | transform);
        }
    };

    /**
     * @brief 邮箱格式化支持
     *
     * 支持的格式符：
     * - #: 输出详细信息
     * - {}: 由嵌套参数动态选择是否输出详细信息，参数类型须为bool
     * @tparam type 元素类型
     */
    template <::verilator_utils::is_format_wrapper_data_type type>
    struct formatter<::verilator_utils::shift_register<type>>
    {
        bool with_detail{};
        ::std::size_t detail_argument_id{::verilator_utils::detail::format_string::npos};

        constexpr auto parse(::std::format_parse_context& ctx)
        {
            return ::verilator_utils::detail::format_string::parse_detail_flag(ctx,
                                                                               "无效的verilator_utils::shift_register格式符"sv,
                                                                               with_detail,
                                                                               detail_argument_id);
        }

        template <typename iter_t>
        auto format(const ::verilator_utils::shift_register<type>& value, ::std::basic_format_context<iter_t, char>& ctx) const
        {
            if(::verilator_utils::detail::format_string::resolve_detail_flag(ctx, with_detail, detail_argument_id))
            {
                return ::std::format_to(ctx.out(), "{{depth: {}, reg: {}}}"sv, value.depth, value.reg);
            }
            return ::std::format_to(ctx.out(), "{}"sv, value.reg);
        }
    };
}  // namespace std

export namespace doctest
{
    template <::std::move_constructible type>
    struct StringMaker<::verilator_utils::mailbox<type>>
    {
        static ::doctest::String convert(const ::verilator_utils::mailbox<type>& value) { return ::std::format("{:#}"sv, value); }
    };

    template <::verilator_utils::is_format_wrapper_data_type type>
    struct StringMaker<::verilator_utils::shift_register<type>>
    {
        static ::doctest::String convert(const ::verilator_utils::shift_register<type>& value)
        { return ::std::format("{:#}"sv, value); }
    };
}  // namespace doctest
