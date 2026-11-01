#include <doctest_macros.hpp>
import unit_test;

namespace
{
    /// 带静态存活计数的move-only类型，用于验证mailbox对元素的析构平衡
    struct lifecycle_counter
    {
        static ::std::size_t live_count;
        int value{};

        explicit lifecycle_counter(int value) : value{value} { ++live_count; }

        lifecycle_counter(const lifecycle_counter&) = delete;
        lifecycle_counter& operator= (const lifecycle_counter&) = delete;

        lifecycle_counter(lifecycle_counter&& other) noexcept : value{::std::exchange(other.value, -1)} { ++live_count; }

        lifecycle_counter& operator= (lifecycle_counter&& other) noexcept
        {
            value = ::std::exchange(other.value, -1);
            return *this;
        }

        ~lifecycle_counter() { --live_count; }
    };

    constinit ::std::size_t lifecycle_counter::live_count{};

}  // namespace

TEST_SUITE("verilator_utils/sync")
{
    using namespace ::std::string_view_literals;

    // NOLINTBEGIN(bugprone-unchecked-optional-access)

    TEST_CASE("mailbox nonblocking operations preserve FIFO order and capacity")
    {
        const scheduler_fixture fixture{};
        ::verilator_utils::mailbox<int> mailbox{2};

        bool put1{};
        bool put2{};
        bool put3{};
        ::std::optional<int> empty_get{};
        bool empty_peek{};
        int peeked_value{};
        ::std::optional<int> first{};
        ::std::optional<int> second{};

        empty_get = mailbox.try_get();
        empty_peek = !mailbox.try_peek().has_value();
        put1 = mailbox.try_put(10);
        put2 = mailbox.try_put(20);
        put3 = mailbox.try_put(30);
        // try_peek 返回只读引用，且不会删除队首元素
        if(auto peeked{mailbox.try_peek()}) { peeked_value = *peeked; }
        first = mailbox.try_get();
        second = mailbox.try_get();

        CHECK(put1);
        CHECK(put2);
        CHECK_FALSE(put3);
        CHECK_FALSE(empty_get.has_value());
        CHECK(empty_peek);
        CHECK_EQ(peeked_value, 10);
        CHECK_EQ(mailbox.num(), 0u);
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());
        CHECK_EQ(*first, 10);
        CHECK_EQ(*second, 20);
    }

    TEST_CASE("unbounded mailbox accepts move-only values")
    {
        const scheduler_fixture fixture{};
        ::verilator_utils::mailbox<::std::unique_ptr<int>> mailbox{};

        bool put_succeeded{};
        put_succeeded = mailbox.try_put(::std::make_unique<int>(42));
        auto value{mailbox.try_get()};

        CHECK(put_succeeded);
        CHECK_EQ(mailbox.num(), 0u);
        REQUIRE(value.has_value());
        REQUIRE(*value);
        CHECK_EQ(**value, 42);
    }

    TEST_CASE("mailbox keeps element construction and destruction balanced")
    {
        CHECK_EQ(lifecycle_counter::live_count, 0u);

        // 有限容量环形缓冲多次回绕后，槽位中的元素被正确析构
        {
            ::verilator_utils::mailbox<lifecycle_counter> mailbox{3};
            CHECK(mailbox.try_put(1));
            CHECK(mailbox.try_get());
            CHECK(mailbox.try_put(2));
            CHECK(mailbox.try_put(3));
            CHECK(mailbox.try_get());
            CHECK(mailbox.try_put(4));
            CHECK_EQ(mailbox.num(), 2u);
        }
        CHECK_EQ(lifecycle_counter::live_count, 0u);

        // 无限容量：消费部分元素后继续放入触发vector扩容，不得泄漏已销毁槽位的元素
        {
            ::verilator_utils::mailbox<lifecycle_counter> mailbox{};
            for(int i{}; i != 5; ++i) { CHECK(mailbox.try_put(i)); }
            CHECK(mailbox.try_get());
            for(int i{}; i != 100; ++i) { CHECK(mailbox.try_put(100 + i)); }
            while(mailbox.try_get()) {}
        }
        CHECK_EQ(lifecycle_counter::live_count, 0u);

        // 无限容量：消费超过水印数量触发前缀擦除，析构计数保持平衡
        {
            ::verilator_utils::mailbox<lifecycle_counter> mailbox{};
            constexpr ::std::size_t count{2048};
            for(auto i{0zu}; i != count; ++i) { CHECK(mailbox.try_put(static_cast<int>(i))); }
            for(auto i{0zu}; i != count; ++i) { CHECK(mailbox.try_get()); }
        }
        CHECK_EQ(lifecycle_counter::live_count, 0u);
    }

    TEST_CASE("mailbox formatter renders values and detailed state")
    {
        const scheduler_fixture fixture{};
        using mailbox_t = ::verilator_utils::mailbox<int>;
        static_assert(::std::formattable<mailbox_t, char>);

        mailbox_t bounded_mailbox{3};
        CHECK(bounded_mailbox.try_put(10));
        CHECK(bounded_mailbox.try_put(20));

        CHECK_EQ(::std::format("{}"sv, bounded_mailbox), "[10, 20]"sv);
        CHECK_EQ(::std::format("{:#}"sv, bounded_mailbox), "{max_count: 3, value: [10, 20]}"sv);
        // 嵌套参数动态选择是否输出详细信息
        CHECK_EQ(::std::format("{:{}}"sv, bounded_mailbox, false), "[10, 20]"sv);
        CHECK_EQ(::std::format("{:{}}"sv, bounded_mailbox, true), "{max_count: 3, value: [10, 20]}"sv);
        CHECK_EQ(::doctest::StringMaker<mailbox_t>::convert(bounded_mailbox), "{max_count: 3, value: [10, 20]}");

        mailbox_t empty_mailbox{};
        CHECK_EQ(::std::format("{:#}"sv, empty_mailbox), "{max_count: 0, value: []}"sv);

        // 无限容量邮箱在非详细模式下同样走无界范围的分支
        mailbox_t unbounded_mailbox{};
        CHECK(unbounded_mailbox.try_put(1));
        CHECK(unbounded_mailbox.try_put(2));
        // 无格式符的替换字段同样走不带详细说明的解析分支
        CHECK_EQ(::std::format("{:}"sv, bounded_mailbox), "[10, 20]"sv);
        CHECK_EQ(::std::format("{}"sv, unbounded_mailbox), "[1, 2]"sv);
        CHECK_EQ(::std::format("{:#}"sv, unbounded_mailbox), "{max_count: 0, value: [1, 2]}"sv);
    }

    TEST_CASE("mailbox formatter rejects malformed dynamic format strings")
    {

        ::verilator_utils::mailbox<int> mailbox{};

        // 不支持的格式符
        CHECK_THROWS_AS(static_cast<void>(::std::vformat("{:x}"sv, ::std::make_format_args(mailbox))), ::std::format_error);
        // 未闭合的替换字段：解析在到达格式串末尾时仍未遇到'}'
        CHECK_THROWS_AS(static_cast<void>(::std::vformat("{:#"sv, ::std::make_format_args(mailbox))), ::std::format_error);
        // 未闭合的嵌套参数
        CHECK_THROWS_AS(static_cast<void>(::std::vformat("{:{}"sv, ::std::make_format_args(mailbox))), ::std::format_error);
        // 嵌套参数只支持自动编号的{}，其它写法在解析阶段被拒绝
        CHECK_THROWS_AS(static_cast<void>(::std::vformat("{:{x}}"sv, ::std::make_format_args(mailbox))), ::std::format_error);
        // 嵌套参数不是bool时被拒绝
        const int non_bool_flag{1};
        CHECK_THROWS_AS(static_cast<void>(::std::vformat("{:{}}"sv, ::std::make_format_args(mailbox, non_bool_flag))),
                        ::std::format_error);
    }

    TEST_CASE("shift_register delays values by the configured depth")
    {
        ::verilator_utils::shift_register<::std::uint64_t> delay_line{3, 8, ::verilator_utils::data_format::hex};

        CHECK_FALSE(delay_line.update(0x01u).has_value());
        CHECK_FALSE(delay_line.update(0x02u).has_value());
        CHECK_FALSE(delay_line.update(0x03u).has_value());

        auto first{delay_line.update(0x04u)};
        REQUIRE(first.has_value());
        CHECK_EQ(first->value(), 0x01u);
        CHECK_EQ(first->width(), 8u);
        CHECK(::std::holds_alternative<::verilator_utils::data_format::hex_t>(first->format()));
        CHECK_EQ(first->to_string(), "0x01"sv);

        auto second{delay_line.update(0x05u)};
        REQUIRE(second.has_value());
        CHECK_EQ(second->value(), 0x02u);
        CHECK_EQ(second->to_string(), "0x02"sv);

        auto third{delay_line.update(0x06u)};
        REQUIRE(third.has_value());
        CHECK_EQ(third->value(), 0x03u);
        CHECK_EQ(third->to_string(), "0x03"sv);
    }

    TEST_CASE("shift_register with depth one echoes the previous value")
    {
        ::verilator_utils::shift_register<::std::uint64_t> delay_line{1, 8, ::verilator_utils::data_format::hex};

        CHECK_FALSE(delay_line.update(0x11u).has_value());

        auto echoed{delay_line.update(0x22u)};
        REQUIRE(echoed.has_value());
        CHECK_EQ(echoed->value(), 0x11u);
        CHECK_EQ(echoed->to_string(), "0x11"sv);

        auto echoed_again{delay_line.update(0x33u)};
        REQUIRE(echoed_again.has_value());
        CHECK_EQ(echoed_again->value(), 0x22u);
    }

    TEST_CASE("shift_register reset clears the delayed values")
    {
        ::verilator_utils::shift_register<::std::uint64_t> delay_line{2, 4, ::verilator_utils::data_format::hex};

        delay_line.update(0x1u);
        delay_line.update(0x2u);
        delay_line.reset();

        CHECK_FALSE(delay_line.update(0x3u).has_value());
        CHECK_FALSE(delay_line.update(0x4u).has_value());
        auto delayed{delay_line.update(0x5u)};
        REQUIRE(delayed.has_value());
        CHECK_EQ(delayed->value(), 0x3u);
    }

    TEST_CASE("shift_register with enable false holds the chain and the output")
    {
        ::verilator_utils::shift_register<::std::uint64_t> delay_line{2, 8, ::verilator_utils::data_format::hex};

        CHECK_FALSE(delay_line.update(0x01u).has_value());
        CHECK_FALSE(delay_line.update(0x02u).has_value());

        // 链未满时，disable 的 update 不产生输出，也不进入寄存器链
        CHECK_FALSE(delay_line.update(0x03u, false).has_value());

        auto first{delay_line.update(0x04u)};
        REQUIRE(first.has_value());
        CHECK_EQ(first->value(), 0x01u);

        // disable 时输出保持上一个移出的值，寄存器链内容不变
        auto held_first{delay_line.update(0x05u, false)};
        REQUIRE(held_first.has_value());
        CHECK_EQ(held_first->value(), 0x01u);
        CHECK_EQ(held_first->width(), 8u);
        CHECK(::std::holds_alternative<::verilator_utils::data_format::hex_t>(held_first->format()));
        CHECK_EQ(held_first->to_string(), "0x01"sv);

        auto held_again{delay_line.update(0x06u, false)};
        REQUIRE(held_again.has_value());
        CHECK_EQ(held_again->value(), 0x01u);

        // 重新使能后，寄存器链未被 disable 期间的值污染
        auto second{delay_line.update(0x07u)};
        REQUIRE(second.has_value());
        CHECK_EQ(second->value(), 0x02u);
    }

    TEST_CASE("shift_register with depth zero outputs and holds the current value")
    {
        ::verilator_utils::shift_register<::std::uint64_t> delay_line{0, 8, ::verilator_utils::data_format::hex};

        auto first{delay_line.update(0x11u)};
        REQUIRE(first.has_value());
        CHECK_EQ(first->value(), 0x11u);

        // disable 时保持最近一次使能输入的值
        auto held{delay_line.update(0x22u, false)};
        REQUIRE(held.has_value());
        CHECK_EQ(held->value(), 0x11u);

        auto second{delay_line.update(0x33u)};
        REQUIRE(second.has_value());
        CHECK_EQ(second->value(), 0x33u);

        delay_line.reset();
        CHECK_FALSE(delay_line.update(0x44u, false).has_value());
        auto third{delay_line.update(0x55u)};
        REQUIRE(third.has_value());
        CHECK_EQ(third->value(), 0x55u);
    }

    TEST_CASE("shift_register reset clears the held output")
    {
        ::verilator_utils::shift_register<::std::uint64_t> delay_line{2, 8, ::verilator_utils::data_format::hex};

        delay_line.update(0x01u);
        delay_line.update(0x02u);
        auto first{delay_line.update(0x03u)};
        REQUIRE(first.has_value());
        CHECK_EQ(first->value(), 0x01u);

        delay_line.reset();

        // reset 后 disable 的 update 不再返回之前保持的输出
        CHECK_FALSE(delay_line.update(0x04u, false).has_value());
        CHECK_FALSE(delay_line.update(0x05u).has_value());
        CHECK_FALSE(delay_line.update(0x06u).has_value());
        auto delayed{delay_line.update(0x07u)};
        REQUIRE(delayed.has_value());
        CHECK_EQ(delayed->value(), 0x05u);
    }

    TEST_CASE("shift_register with enable supports Verilator wide data")
    {
        ::verilator_utils::shift_register<::VlWide<2>> delay_line{1, 48, ::verilator_utils::data_format::hex};

        const ::VlWide<2> first_value{0x89ab'cdefu, 0x0000'0123u};
        CHECK_FALSE(delay_line.update(first_value).has_value());

        // disable 的输入不进入寄存器链
        const ::VlWide<2> disabled_value{0xdead'beefu, 0x0000'0deau};
        CHECK_FALSE(delay_line.update(disabled_value, false).has_value());

        const ::VlWide<2> second_value{0x1122'3344u, 0x0000'0001u};
        auto delayed{delay_line.update(second_value)};
        REQUIRE(delayed.has_value());
        CHECK_EQ(delayed->value().at(0), 0x89ab'cdefu);
        CHECK_EQ(delayed->value().at(1), 0x0000'0123u);

        // disable 时输出保持上一个移出的宽数据
        auto held{delay_line.update(second_value, false)};
        REQUIRE(held.has_value());
        CHECK_EQ(held->value().at(0), 0x89ab'cdefu);
        CHECK_EQ(held->value().at(1), 0x0000'0123u);
        CHECK_EQ(held->to_string(), "0x012389abcdef"sv);
    }

    TEST_CASE("shift_register preserves the configured data format")
    {
        ::verilator_utils::shift_register<::std::uint64_t> binary_delay_line{
            2,
            {4, ::verilator_utils::data_format::bin}
        };
        binary_delay_line.update(0xbu);
        binary_delay_line.update(0xcu);
        auto binary_value{binary_delay_line.update(0xdu)};
        REQUIRE(binary_value.has_value());
        CHECK_EQ(binary_value->value(), 0xbu);
        CHECK_EQ(binary_value->width(), 4u);
        CHECK(::std::holds_alternative<::verilator_utils::data_format::bin_t>(binary_value->format()));
        CHECK_EQ(binary_value->to_string(), "0b1011"sv);

        ::verilator_utils::shift_register<::std::uint64_t> decimal_delay_line{2, 8, ::verilator_utils::data_format::dec_unsigned};
        decimal_delay_line.update(42u);
        decimal_delay_line.update(43u);
        auto decimal_value{decimal_delay_line.update(44u)};
        REQUIRE(decimal_value.has_value());
        CHECK_EQ(decimal_value->value(), 42u);
        CHECK(::std::holds_alternative<::verilator_utils::data_format::dec_unsigned_t>(decimal_value->format()));
        CHECK_EQ(decimal_value->to_string(), "42"sv);
    }

    TEST_CASE("shift_register supports Verilator wide data")
    {
        ::verilator_utils::shift_register<::VlWide<2>> delay_line{2, 48, ::verilator_utils::data_format::hex};

        const ::VlWide<2> first_value{0x89ab'cdefu, 0x0000'0123u};
        CHECK_FALSE(delay_line.update(first_value).has_value());

        const ::VlWide<2> second_value{0x1122'3344u, 0x0000'0001u};
        CHECK_FALSE(delay_line.update(second_value).has_value());

        const ::VlWide<2> third_value{0x5566'7788u, 0x0000'0002u};
        auto delayed{delay_line.update(third_value)};
        REQUIRE(delayed.has_value());
        CHECK_EQ(delayed->value().at(0), 0x89ab'cdefu);
        CHECK_EQ(delayed->value().at(1), 0x0000'0123u);
        CHECK_EQ(delayed->width(), 48u);
        CHECK(::std::holds_alternative<::verilator_utils::data_format::hex_t>(delayed->format()));
        CHECK_EQ(delayed->to_string(), "0x012389abcdef"sv);

        const ::VlWide<2> fourth_value{0xaabb'ccddu, 0x0000'0003u};
        auto delayed_again{delay_line.update(fourth_value)};
        REQUIRE(delayed_again.has_value());
        CHECK_EQ(delayed_again->value().at(0), 0x1122'3344u);
        CHECK_EQ(delayed_again->value().at(1), 0x0000'0001u);
    }

    TEST_CASE("shift_register formatter renders contents and detailed state")
    {

        using shift_register_t = ::verilator_utils::shift_register<::std::uint64_t>;
        static_assert(::std::formattable<shift_register_t, char>);

        shift_register_t delay_line{3, 8, ::verilator_utils::data_format::hex};
        delay_line.update(0x01u);
        delay_line.update(0x02u);
        delay_line.update(0x03u);

        CHECK_EQ(::std::format("{}"sv, delay_line), "[1, 2, 3]"sv);
        CHECK_EQ(::std::format("{:#}"sv, delay_line), "{depth: 3, reg: [1, 2, 3]}"sv);
        // 嵌套参数动态选择是否输出详细信息
        CHECK_EQ(::std::format("{:{}}"sv, delay_line, false), "[1, 2, 3]"sv);
        CHECK_EQ(::std::format("{:{}}"sv, delay_line, true), "{depth: 3, reg: [1, 2, 3]}"sv);
        CHECK_EQ(::doctest::StringMaker<shift_register_t>::convert(delay_line), "{depth: 3, reg: [1, 2, 3]}");
    }

    TEST_CASE("shift_register formatter rejects unsupported format specifiers")
    {

        ::verilator_utils::shift_register<::std::uint64_t> delay_line{1, 8, ::verilator_utils::data_format::hex};

        CHECK_THROWS_AS(static_cast<void>(::std::vformat("{:x}"sv, ::std::make_format_args(delay_line))), ::std::format_error);
        // 嵌套参数不是bool时被拒绝
        const int non_bool_flag{1};
        CHECK_THROWS_AS(static_cast<void>(::std::vformat("{:{}}"sv, ::std::make_format_args(delay_line, non_bool_flag))),
                        ::std::format_error);
    }

    TEST_CASE("sync formatters write into a raw character buffer")
    {
        ::verilator_utils::mailbox<int> mailbox{2};
        CHECK(mailbox.try_put(7));
        ::verilator_utils::shift_register<::std::uint64_t> delay_line{1, 8, ::verilator_utils::data_format::hex};
        CHECK_FALSE(delay_line.update(0xabu).has_value());

        ::std::array<char, 512> detail_buffer{};
        auto* const mailbox_end{::std::format_to(detail_buffer.data(), "{:#}"sv, mailbox)};
        CHECK_EQ(::std::string_view{detail_buffer.data(),
                                    static_cast<::std::size_t>(::std::distance(detail_buffer.data(), mailbox_end))},
                 ::std::format("{:#}"sv, mailbox));

        auto* const delay_end{::std::format_to(detail_buffer.data(), "{}"sv, delay_line)};
        CHECK_EQ(::std::string_view{detail_buffer.data(),
                                    static_cast<::std::size_t>(::std::distance(detail_buffer.data(), delay_end))},
                 ::std::format("{}"sv, delay_line));
    }

    TEST_CASE("mailbox get and peek wait until a value is available")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::mailbox<int> mailbox{};
        const int* peeked{};
        int received{};

        auto consumer_task{[&](this auto) -> ::verilator_utils::task<void> {
            const int& reference{co_await mailbox.peek()};
            peeked = ::std::addressof(reference);
            received = co_await mailbox.get();
        }()};
        scheduler.add_task(::std::move(consumer_task));

        scheduler.loop_once();
        CHECK_FALSE(peeked);
        CHECK_EQ(mailbox.num(), 0u);

        bool put_succeeded{};
        put_succeeded = mailbox.try_put(17);
        scheduler.loop_once();
        CHECK(put_succeeded);

        CHECK(peeked);
        CHECK_EQ(received, 17);
        CHECK_EQ(mailbox.num(), 0u);
    }

    TEST_CASE("const mailbox peek operations preserve const reference identity")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::mailbox<int> mailbox{};
        const auto& const_mailbox{mailbox};
        int peeked{};

        // 阻塞式peek需要修改事件等待队列，仅支持非const邮箱；
        static_assert(::std::same_as<decltype(const_mailbox.try_peek()), ::std::optional<int>>);
        CHECK_FALSE(const_mailbox.try_peek().has_value());
        CHECK_EQ(const_mailbox.num(), 0u);

        bool put_succeeded{};
        put_succeeded = mailbox.try_put(29);
        CHECK(put_succeeded);

        // 通过非const阻塞式peek获取队首元素，用于验证元素的身份
        auto peeker_task{[&](this auto) -> ::verilator_utils::task<void> { peeked = co_await mailbox.peek(); }()};
        scheduler.add_task(::std::move(peeker_task));
        scheduler.loop_once();
        CHECK_EQ(peeked, 29);

        // const mailbox的try_peek返回同一元素，且不删除元素
        auto nonblocking_peek{const_mailbox.try_peek()};
        REQUIRE(nonblocking_peek.has_value());
        CHECK_EQ(*nonblocking_peek, 29);
        CHECK_EQ(peeked, *nonblocking_peek);
        CHECK_EQ(const_mailbox.num(), 1u);
    }

    TEST_CASE("bounded mailbox put waits for available capacity")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::mailbox<int> mailbox{1};
        bool producer_completed{};

        bool initial_put{};
        initial_put = mailbox.try_put(1);
        CHECK(initial_put);

        auto producer_task{[&](this auto) -> ::verilator_utils::task<void> {
            co_await mailbox.put(2);
            producer_completed = true;
        }()};
        scheduler.add_task(::std::move(producer_task));

        scheduler.loop_once();
        CHECK_FALSE(producer_completed);
        CHECK_EQ(mailbox.num(), 1u);

        ::std::optional<int> first{};
        first = mailbox.try_get();
        scheduler.loop_once();
        REQUIRE(first.has_value());
        CHECK_EQ(*first, 1);
        CHECK(producer_completed);
        CHECK_EQ(mailbox.num(), 1u);

        ::std::optional<int> second{};
        second = mailbox.try_get();
        scheduler.loop_once();
        REQUIRE(second.has_value());
        CHECK_EQ(*second, 2);
        CHECK_EQ(mailbox.num(), 0u);
    }

    TEST_CASE("semaphore nonblocking operations update the available count")
    {
        ::verilator_utils::semaphore semaphore{3};

        CHECK(semaphore.try_get(2));
        CHECK_FALSE(semaphore.try_get(2));
        CHECK(semaphore.try_get());
        CHECK_FALSE(semaphore.try_get());
        semaphore.put(4);
        CHECK(semaphore.try_get(4));
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("semaphore get waits for enough keys")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{1};
        bool acquired{};

        auto waiter_task{[&](this auto) -> ::verilator_utils::task<void> {
            co_await semaphore.get(2);
            acquired = true;
        }()};
        scheduler.add_task(::std::move(waiter_task));

        scheduler.loop_once();
        CHECK_FALSE(acquired);
        semaphore.put();
        scheduler.loop_once();

        CHECK(acquired);
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("semaphore grants blocked waiters in ticket order")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{};
        ::std::vector<int> acquisition_order;

        const auto make_waiter{[&](this auto, int id) -> ::verilator_utils::task<void> {
            co_await semaphore.get();
            acquisition_order.push_back(id);
        }};
        auto first_task{make_waiter(1)};
        auto second_task{make_waiter(2)};
        scheduler.add_task(::std::move(first_task));
        scheduler.add_task(::std::move(second_task));

        scheduler.loop_once();
        CHECK(acquisition_order.empty());

        semaphore.put();
        scheduler.loop_once();
        CHECK_EQ(acquisition_order, ::std::vector<int>{1});

        semaphore.put();
        scheduler.loop_once();
        CHECK_EQ(acquisition_order, (::std::vector<int>{1, 2}));
    }

    TEST_CASE("semaphore get returns immediately when the count already suffices")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{3};
        bool acquired{};

        auto waiter_task{[&](this auto) -> ::verilator_utils::task<void> {
            co_await semaphore.get(2);
            acquired = true;
        }()};
        scheduler.add_task(::std::move(waiter_task));

        scheduler.loop_once();
        CHECK(acquired);
        CHECK(semaphore.try_get());
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("semaphore reserves permits for queued waiters before waking them")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{};
        ::std::vector<int> acquisition_order;

        const auto make_waiter{[&](this auto, int id) -> ::verilator_utils::task<void> {
            co_await semaphore.get();
            acquisition_order.push_back(id);
        }};
        auto first_task{make_waiter(1)};
        auto second_task{make_waiter(2)};
        scheduler.add_task(::std::move(first_task));
        scheduler.add_task(::std::move(second_task));

        scheduler.loop_once();
        CHECK(acquisition_order.empty());

        // 放入的许可立即预留给队首等待者：外部观察者（如同伴协程）在等待者恢复前无法抢走，
        // 因此这里 try_get 必须失败（旧实现中许可尚未预留，try_get 会成功）
        semaphore.put();
        CHECK_FALSE(semaphore.try_get());
        CHECK(acquisition_order.empty());

        scheduler.loop_once();
        CHECK_EQ(acquisition_order, ::std::vector<int>{1});
        CHECK_FALSE(semaphore.try_get());

        // 下一个许可同样预留给新的队首等待者
        semaphore.put();
        scheduler.loop_once();
        CHECK_EQ(acquisition_order, (::std::vector<int>{1, 2}));
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("semaphore grants multiple queued waiters from a single put in FIFO order")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{};
        ::std::vector<int> acquisition_order;

        const auto make_waiter{[&](this auto, int id, ::std::size_t update) -> ::verilator_utils::task<void> {
            co_await semaphore.get(update);
            acquisition_order.push_back(id);
        }};
        auto first_task{make_waiter(1, 1)};
        auto second_task{make_waiter(2, 2)};
        auto third_task{make_waiter(3, 3)};
        scheduler.add_task(::std::move(first_task));
        scheduler.add_task(::std::move(second_task));
        scheduler.add_task(::std::move(third_task));

        scheduler.loop_once();
        CHECK(acquisition_order.empty());

        // 一次放入5个许可：按FIFO满足前两个等待者（1+2），剩余2个不足以满足第三个
        semaphore.put(5);
        scheduler.loop_once();
        CHECK_EQ(acquisition_order, ::std::vector<int>{1, 2});
        // 严格FIFO：队列非空时，即使计数足以满足获取，try_get也必须失败
        CHECK_FALSE(semaphore.try_get(3));
        CHECK_FALSE(semaphore.try_get(2));

        // 补足最后一个等待者所需的许可
        semaphore.put(1);
        scheduler.loop_once();
        CHECK_EQ(acquisition_order, (::std::vector<int>{1, 2, 3}));
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("semaphore keeps FIFO order when the head waiter needs more permits than are available")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{};
        ::std::vector<int> acquisition_order;

        const auto make_waiter{[&](this auto, int id, ::std::size_t update) -> ::verilator_utils::task<void> {
            co_await semaphore.get(update);
            acquisition_order.push_back(id);
        }};
        auto first_task{make_waiter(1, 3)};
        auto second_task{make_waiter(2, 1)};
        scheduler.add_task(::std::move(first_task));
        scheduler.add_task(::std::move(second_task));

        scheduler.loop_once();
        CHECK(acquisition_order.empty());

        // 队首等待者需要3个许可：放入2个后仍不足，排在后面的等待者不能插队
        semaphore.put(2);
        scheduler.loop_once();
        CHECK(acquisition_order.empty());
        // 严格FIFO：等待队列非空时，任何非阻塞获取都不得拿走许可，即使计数足以满足
        CHECK_FALSE(semaphore.try_get(3));
        CHECK_FALSE(semaphore.try_get(2));

        // 补足队首等待者所需的许可（仅满足队首），后续等待者仍然等待
        semaphore.put(1);
        scheduler.loop_once();
        CHECK_EQ(acquisition_order, ::std::vector<int>{1});
        CHECK_FALSE(semaphore.try_get());

        // 队首许可全部发放后，后续等待者才获得许可
        semaphore.put();
        scheduler.loop_once();
        CHECK_EQ(acquisition_order, (::std::vector<int>{1, 2}));
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("semaphore waiters cancelled by the end of the simulation release their frames")
    {
        ::std::size_t destroyed{};
        {
            scheduler_fixture fixture{};
            auto scheduler{fixture.make_scheduler()};
            // 同步体在调度器之后声明，因此先于调度器析构：此时仿真已结束，
            // 被唤醒的等待者不得再访问已析构的同步体
            ::verilator_utils::semaphore semaphore{};

            const auto waiter{[&](this auto) -> ::verilator_utils::task<void> {
                const frame_destruction_counter counter{&destroyed};
                co_await semaphore.get();
            }};
            const auto finish{[&](this auto) -> ::verilator_utils::task<void> {
                const frame_destruction_counter counter{&destroyed};
                co_await ::verilator_utils::wait_time(1_ns);
                co_await ::verilator_utils::eval_finish();
            }};
            scheduler.add_task(waiter());
            scheduler.add_task(finish());

            // 仿真通过eval_finish结束，但信号量等待者仍挂起：死锁诊断抛出runtime_error
            CHECK_THROWS_AS(scheduler.loop_until_finish(), ::std::runtime_error);
            CHECK_EQ(destroyed, 1zu);
        }
        // 调度器析构时回收仍挂起在信号量上的等待者
        CHECK_EQ(destroyed, 2zu);
    }

    TEST_CASE("semaphore waiters are reclaimed when the scheduler is destroyed first")
    {
        ::std::size_t destroyed{};
        {
            scheduler_fixture fixture{};
            // 同步体比调度器长寿：调度器析构时回收挂起任务，并向同步体解除绑定，
            // 因此同步体析构时不应再访问已经析构的调度器
            ::verilator_utils::semaphore semaphore{};
            {
                auto scheduler{fixture.make_scheduler()};

                const auto waiter{[&](this auto) -> ::verilator_utils::task<void> {
                    const frame_destruction_counter counter{&destroyed};
                    co_await semaphore.get();
                }};
                const auto finish{[&](this auto) -> ::verilator_utils::task<void> {
                    const frame_destruction_counter counter{&destroyed};
                    co_await ::verilator_utils::wait_time(1_ns);
                    co_await ::verilator_utils::eval_finish();
                }};
                scheduler.add_task(waiter());
                scheduler.add_task(finish());

                // 仿真通过eval_finish结束，但信号量等待者仍挂起：死锁诊断抛出runtime_error
                CHECK_THROWS_AS(scheduler.loop_until_finish(), ::std::runtime_error);
                CHECK_EQ(destroyed, 1zu);
            }
            // 调度器析构时回收仍挂起在信号量上的等待者
            CHECK_EQ(destroyed, 2zu);
        }
        CHECK_EQ(destroyed, 2zu);
    }

    TEST_CASE("semaphore get enqueues behind queued waiters even when the count suffices")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{};
        ::std::vector<int> acquisition_order;

        const auto make_waiter{[&](this auto, int id, ::std::size_t update) -> ::verilator_utils::task<void> {
            co_await semaphore.get(update);
            acquisition_order.push_back(id);
        }};
        auto first_task{make_waiter(1, 5)};
        auto second_task{make_waiter(2, 1)};
        scheduler.add_task(::std::move(first_task));
        scheduler.add_task(::std::move(second_task));

        scheduler.loop_once();
        CHECK(acquisition_order.empty());

        // 队首需要5个许可：放入4个后队首仍不满足，此时新来的get(1)计数已足够，
        // 但严格FIFO要求它排到队尾而不是立即获得许可
        semaphore.put(4);
        scheduler.loop_once();
        CHECK(acquisition_order.empty());
        CHECK_FALSE(semaphore.try_get());

        // 补足队首的许可：队首获得许可后计数耗尽，排队的get(1)继续等待
        semaphore.put(1);
        scheduler.loop_once();
        CHECK_EQ(acquisition_order, ::std::vector<int>{1});
        CHECK_FALSE(semaphore.try_get());

        // 新的许可到达后才轮到排队的get(1)
        semaphore.put();
        scheduler.loop_once();
        CHECK_EQ(acquisition_order, (::std::vector<int>{1, 2}));
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("semaphore restores nonblocking acquisition once the queue drains")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{};

        bool acquired{};
        auto waiter_task{[&](this auto) -> ::verilator_utils::task<void> {
            co_await semaphore.get();
            acquired = true;
        }()};
        scheduler.add_task(::std::move(waiter_task));

        scheduler.loop_once();
        CHECK_FALSE(acquired);

        // 放入2个许可：1个发放给队首等待者，剩余1个保留在计数中，
        // 队列已逻辑清空（物理条目要等到回收水位才被擦除），非阻塞获取立即恢复
        semaphore.put(2);
        CHECK(semaphore.try_get());
        CHECK_FALSE(semaphore.try_get());
        scheduler.loop_once();
        CHECK(acquired);

        // 队列清空后，计数充足的get走快速路径，一次调度即完成
        bool fast_acquired{};
        auto fast_task{[&](this auto) -> ::verilator_utils::task<void> {
            co_await semaphore.get();
            fast_acquired = true;
        }()};
        semaphore.put();
        scheduler.add_task(::std::move(fast_task));
        scheduler.loop_once();
        CHECK(fast_acquired);
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("semaphore waiters accumulate permits across multiple puts")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{};
        bool acquired{};

        auto waiter_task{[&](this auto) -> ::verilator_utils::task<void> {
            co_await semaphore.get(5);
            acquired = true;
        }()};
        scheduler.add_task(::std::move(waiter_task));

        scheduler.loop_once();
        CHECK_FALSE(acquired);

        // 放入0个许可不得发放任何等待者
        semaphore.put(0);
        scheduler.loop_once();
        CHECK_FALSE(acquired);

        semaphore.put(2);
        scheduler.loop_once();
        CHECK_FALSE(acquired);

        semaphore.put(2);
        scheduler.loop_once();
        CHECK_FALSE(acquired);

        // 累计许可达到请求量后一次性发放
        semaphore.put();
        scheduler.loop_once();
        CHECK(acquired);
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("semaphore grants a large queue of waiters without losing or duplicating permits")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{};
        // 超过semaphore内部suspend_queue的回收水位线，强制触发前缀回收路径
        constexpr static ::std::size_t waiter_count{4096zu / sizeof(::std::size_t) + 8zu};
        constexpr static auto iota_range{::std::views::iota(0zu, waiter_count)};
        ::std::vector<::std::size_t> acquisition_order;
        acquisition_order.reserve(waiter_count);

        const auto waiter{[&](this auto, ::std::size_t id) -> ::verilator_utils::task<void> {
            co_await semaphore.get();
            acquisition_order.push_back(id);
        }};

        for(const auto i: iota_range) { scheduler.add_task(waiter(i)); }

        scheduler.loop_once();
        CHECK_EQ(acquisition_order.size(), 0u);

        // 一次发放所有许可，跨过回收边界后仍按原FIFO顺序完成
        semaphore.put(waiter_count);
        scheduler.loop_once();

        REQUIRE_EQ(acquisition_order.size(), waiter_count);
        const auto expected{iota_range | ::std::ranges::to<::std::vector>()};
        CHECK_EQ(acquisition_order, expected);
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("semaphore reclaims queue storage for very large waiter queues")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{};
        // 等待者数量足够多，使前缀回收后的空闲容量达到收缩阈值，触发shrink_to_fit
        constexpr static ::std::size_t waiter_count{6000};
        ::std::size_t completed{};

        const auto waiter{[&](this auto, ::std::size_t update) -> ::verilator_utils::task<void> {
            co_await semaphore.get(update);
            ++completed;
        }};

        scheduler.add_task(waiter(2));
        for(::std::size_t i{1}; i != waiter_count; ++i) { scheduler.add_task(waiter(1)); }

        scheduler.loop_once();
        CHECK_EQ(completed, 0u);

        // 总需求量恰好为waiter_count+1，发放后不留余量，验证计数守恒
        semaphore.put(waiter_count + 1);
        scheduler.loop_once();

        CHECK_EQ(completed, waiter_count);
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("mailbox put rechecks capacity when a peer producer fills the freed slot")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::mailbox<int> mailbox{2};
        ::std::size_t completed_count{};
        ::std::size_t max_observed_size{};
        bool capacity_violation{};

        // 填满邮箱，使所有生产者都阻塞
        bool initial_put1{};
        bool initial_put2{};
        initial_put1 = mailbox.try_put(97);
        initial_put2 = mailbox.try_put(98);
        REQUIRE(initial_put1);
        REQUIRE(initial_put2);

        const auto make_producer{[&](this auto, int value) -> ::verilator_utils::task<void> {
            co_await mailbox.put(value);
            ++completed_count;
            max_observed_size = ::std::max(max_observed_size, mailbox.num());
            capacity_violation = capacity_violation || mailbox.num() > 2;
        }};
        auto first_task{make_producer(10)};
        auto second_task{make_producer(20)};
        auto third_task{make_producer(30)};
        scheduler.add_task(::std::move(first_task));
        scheduler.add_task(::std::move(second_task));
        scheduler.add_task(::std::move(third_task));

        scheduler.loop_once();
        CHECK_EQ(completed_count, 0u);
        CHECK_EQ(mailbox.num(), 2u);

        // 消费一个元素释放一个空位：阻塞的生产者被唤醒并放入，其余继续等待
        ::std::optional<int> first_item{};
        first_item = mailbox.try_get();
        scheduler.loop_once();
        REQUIRE(first_item.has_value());
        CHECK_EQ(*first_item, 97);
        CHECK_EQ(completed_count, 1u);
        CHECK_EQ(mailbox.num(), 2u);

        // 再次释放空位：又一个生产者放入，其余继续等待
        ::std::optional<int> second_item{};
        second_item = mailbox.try_get();
        scheduler.loop_once();
        REQUIRE(second_item.has_value());
        CHECK_EQ(*second_item, 98);
        CHECK_EQ(completed_count, 2u);
        CHECK_EQ(mailbox.num(), 2u);

        // 最后一个生产者最终放入
        ::std::optional<int> third_item{};
        third_item = mailbox.try_get();
        scheduler.loop_once();
        REQUIRE(third_item.has_value());
        CHECK_EQ(completed_count, 3u);
        CHECK_EQ(mailbox.num(), 2u);

        // 所有元素均按FIFO顺序取出，容量从未被突破
        ::std::vector<int> drained{*first_item, *second_item, *third_item};
        while(auto item{mailbox.try_get()}) { drained.push_back(*item); }
        ::std::ranges::sort(drained);
        CHECK_EQ(drained, (::std::vector<int>{10, 20, 30, 97, 98}));
        CHECK_FALSE(capacity_violation);
        CHECK_LE(max_observed_size, 2u);
    }

    TEST_CASE("mailbox get rechecks emptiness when a peer consumer takes the only item")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::mailbox<int> mailbox{};
        int first_received{};
        int second_received{};

        const auto make_consumer{
            [&](this auto, int& received) -> ::verilator_utils::task<void> { received = co_await mailbox.get(); }};
        auto first_task{make_consumer(first_received)};
        auto second_task{make_consumer(second_received)};
        scheduler.add_task(::std::move(first_task));
        scheduler.add_task(::std::move(second_task));

        scheduler.loop_once();
        CHECK_EQ(first_received, 0);
        CHECK_EQ(second_received, 0);

        // 只放入一个元素：仅一个消费者被唤醒并取出，
        // 另一个消费者必须重新检查空态而不是对空邮箱取值
        bool put1{};
        put1 = mailbox.try_put(5);
        scheduler.loop_once();
        REQUIRE(put1);
        CHECK_EQ(first_received, 5);
        CHECK_EQ(second_received, 0);
        CHECK_EQ(mailbox.num(), 0u);

        // 第二个消费者重新等待后获得新元素
        bool put2{};
        put2 = mailbox.try_put(6);
        scheduler.loop_once();
        REQUIRE(put2);
        CHECK_EQ(second_received, 6);
        CHECK_EQ(mailbox.num(), 0u);
    }

    TEST_CASE("mailbox peek rechecks emptiness when a consumer removes the only item")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::mailbox<int> mailbox{};
        int received{};
        int peeked{};

        auto getter_task{[&](this auto) -> ::verilator_utils::task<void> { received = co_await mailbox.get(); }()};
        auto peeker_task{[&](this auto) -> ::verilator_utils::task<void> { peeked = co_await mailbox.peek(); }()};
        scheduler.add_task(::std::move(getter_task));
        scheduler.add_task(::std::move(peeker_task));

        scheduler.loop_once();
        CHECK_EQ(received, 0);
        CHECK_EQ(peeked, 0);

        // 只放入一个元素：getter取出后邮箱变空，peeker必须重新等待
        bool put1{};
        put1 = mailbox.try_put(7);
        scheduler.loop_once();
        REQUIRE(put1);
        CHECK_EQ(received, 7);
        CHECK_EQ(peeked, 0);
        CHECK_EQ(mailbox.num(), 0u);

        // peeker重新等待后观察到新元素，且不删除它
        bool put2{};
        put2 = mailbox.try_put(8);
        scheduler.loop_once();
        REQUIRE(put2);
        CHECK_EQ(peeked, 8);
        CHECK_EQ(mailbox.num(), 1u);

        ::std::optional<int> item{};
        item = mailbox.try_get();
        REQUIRE(item.has_value());
        CHECK_EQ(*item, 8);
        CHECK_EQ(mailbox.num(), 0u);
    }

    TEST_CASE("concurrent mailbox producers and consumers converge without exceeding capacity")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::mailbox<int> mailbox{3};
        constexpr static ::std::size_t producer_count{4};
        constexpr static ::std::size_t consumer_count{4};
        constexpr static ::std::size_t items_per_task{5};
        ::std::vector<int> received;
        ::std::size_t max_observed_size{};
        bool capacity_violation{};

        const auto producer{[&](this auto, int id) -> ::verilator_utils::task<void> {
            for(auto i{0zu}; i != items_per_task; ++i)
            {
                co_await mailbox.put((id * 100 + static_cast<int>(i)));
                max_observed_size = ::std::max(max_observed_size, mailbox.num());
                capacity_violation = capacity_violation || mailbox.num() > 3;
            }
        }};
        const auto consumer{[&](this auto) -> ::verilator_utils::task<void> {
            for(auto i{0zu}; i != items_per_task; ++i) { received.push_back(co_await mailbox.get()); }
        }};

        for(auto i{0zu}; i != producer_count; ++i) { scheduler.add_task(producer(static_cast<int>(i))); }
        for(auto i{0zu}; i != consumer_count; ++i) { scheduler.add_task(consumer()); }

        scheduler.loop_until_finish();

        CHECK_FALSE(capacity_violation);
        CHECK_LE(max_observed_size, 3u);
        CHECK_EQ(mailbox.num(), 0u);

        ::std::vector<int> expected;
        expected.reserve(producer_count * items_per_task);
        for(::std::size_t id{}; id != producer_count; ++id)
        {
            for(auto i{0zu}; i != items_per_task; ++i) { expected.push_back(static_cast<int>(id * 100 + i)); }
        }
        ::std::ranges::sort(received);
        CHECK_EQ(received, expected);
    }

    TEST_CASE("event suspends waiters until notify_all wakes every waiter in order")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::event event{};
        ::std::vector<int> wake_order;

        const auto make_waiter{[&](this auto, int id) -> ::verilator_utils::task<void> {
            co_await event;
            wake_order.push_back(id);
        }};
        auto first_task{make_waiter(1)};
        auto second_task{make_waiter(2)};
        auto third_task{make_waiter(3)};
        scheduler.add_task(::std::move(first_task));
        scheduler.add_task(::std::move(second_task));
        scheduler.add_task(::std::move(third_task));

        scheduler.loop_once();
        CHECK(wake_order.empty());

        // notify_all 为同步调用，直接唤醒所有等待者
        event.notify_all();
        scheduler.loop_once();
        CHECK_EQ(wake_order, (::std::vector<int>{1, 2, 3}));
    }

    TEST_CASE("event notify_one wakes the oldest waiter and preserves FIFO order")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::event event{};
        ::std::vector<int> wake_order;

        const auto make_waiter{[&](this auto, int id) -> ::verilator_utils::task<void> {
            co_await event;
            wake_order.push_back(id);
        }};
        auto first_task{make_waiter(1)};
        auto second_task{make_waiter(2)};
        auto third_task{make_waiter(3)};
        scheduler.add_task(::std::move(first_task));
        scheduler.add_task(::std::move(second_task));
        scheduler.add_task(::std::move(third_task));

        scheduler.loop_once();
        CHECK(wake_order.empty());

        event.notify_one();
        scheduler.loop_once();
        CHECK_EQ(wake_order, ::std::vector<int>{1});

        event.notify_one();
        scheduler.loop_once();
        CHECK_EQ(wake_order, (::std::vector<int>{1, 2}));

        event.notify_one();
        scheduler.loop_once();
        CHECK_EQ(wake_order, (::std::vector<int>{1, 2, 3}));
    }

    TEST_CASE("event is edge-triggered and does not latch missed notifications")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::event event{};

        // 在没有任何等待者时通知：通知被丢弃，随后到达的等待者仍会挂起
        event.notify_all();

        bool woke{};
        auto waiter_task{[&](this auto) -> ::verilator_utils::task<void> {
            co_await event;
            woke = true;
        }()};
        scheduler.add_task(::std::move(waiter_task));

        scheduler.loop_once();
        CHECK_FALSE(woke);

        // 再次通知后等待者才被唤醒
        event.notify_all();
        scheduler.loop_once();
        CHECK(woke);
    }

    TEST_CASE("event notify_one on an empty queue is a safe no-op")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::event event{};

        // 在没有任何等待者时通知单个协程：不崩溃、不做任何事，随后到达的等待者仍会挂起
        event.notify_one();

        bool woke{};
        auto waiter_task{[&](this auto) -> ::verilator_utils::task<void> {
            co_await event;
            woke = true;
        }()};
        scheduler.add_task(::std::move(waiter_task));
        scheduler.loop_once();
        CHECK_FALSE(woke);

        // 再次通知后等待者才被唤醒
        event.notify_one();
        scheduler.loop_once();
        CHECK(woke);
    }

    TEST_CASE("event notify_all on an empty queue is a safe no-op")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::event event{};

        // 在没有任何等待者时通知所有协程：不崩溃、不做任何事，随后到达的等待者仍会挂起
        event.notify_all();

        bool woke{};
        auto waiter_task{[&](this auto) -> ::verilator_utils::task<void> {
            co_await event;
            woke = true;
        }()};
        scheduler.add_task(::std::move(waiter_task));
        scheduler.loop_once();
        CHECK_FALSE(woke);

        // 再次通知后等待者才被唤醒
        event.notify_all();
        scheduler.loop_once();
        CHECK(woke);
    }

    TEST_CASE("event can be awaited again after each notification")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::event event{};
        ::std::size_t wake_count{};

        auto waiter_task{[&](this auto) -> ::verilator_utils::task<void> {
            for(auto i{0zu}; i != 3; ++i)
            {
                co_await event;
                ++wake_count;
            }
        }()};
        scheduler.add_task(::std::move(waiter_task));

        scheduler.loop_once();
        CHECK_EQ(wake_count, 0u);

        for(::std::size_t i{1}; i != 4; ++i)
        {
            event.notify_all();
            scheduler.loop_once();
            CHECK_EQ(wake_count, i);
        }
    }

    TEST_CASE("event handshake between producer and consumer converges")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        constexpr static ::std::size_t item_count{5};
        ::verilator_utils::event data_ready{};
        ::verilator_utils::event data_consumed{};
        ::std::vector<int> received;

        // 消费者先等待数据，生产者发送数据后等待消费者确认
        const auto consumer{[&](this auto) -> ::verilator_utils::task<void> {
            for(auto i{0zu}; i != item_count; ++i)
            {
                co_await data_ready;
                received.push_back(static_cast<int>(i));
                data_consumed.notify_all();
            }
        }};
        const auto producer{[&](this auto) -> ::verilator_utils::task<void> {
            for(auto i{0zu}; i != item_count; ++i)
            {
                data_ready.notify_all();
                co_await data_consumed;
            }
        }};

        auto consumer_task{consumer()};
        scheduler.add_task(::std::move(consumer_task));

        // 消费者先就绪等待数据
        scheduler.loop_once();
        CHECK(received.empty());

        auto producer_task{producer()};
        scheduler.add_task(::std::move(producer_task));
        scheduler.loop_until_finish();

        ::std::vector<int> expected;
        expected.reserve(item_count);
        for(auto i{0zu}; i != item_count; ++i) { expected.push_back(static_cast<int>(i)); }
        CHECK_EQ(received, expected);
    }

    TEST_CASE("event wakes a large batch of waiters and stays usable afterwards")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::event event{};
        // 等待者数量使挂起队列的空闲容量达到收缩阈值：erase_watermark为256，shrink_watermark为其4倍
        constexpr static ::std::size_t waiter_count{1024};
        ::std::size_t completed{};

        const auto waiter_lambda{
            [&] -> ::verilator_utils::task<void> {
                co_await event;
                ++completed;
            },
        };

        for(auto i{0zu}; i != waiter_count; ++i) { scheduler.add_task(waiter_lambda()); }
        scheduler.loop_once();
        CHECK_EQ(completed, 0u);

        // 一次唤醒全部等待者，清空后的空闲容量超过阈值，触发内存回收
        event.notify_all();
        scheduler.loop_once();
        CHECK_EQ(completed, waiter_count);

        // 收缩后事件仍然可用
        completed = 0;
        scheduler.add_task(waiter_lambda());
        scheduler.loop_once();
        CHECK_EQ(completed, 0u);
        event.notify_all();
        scheduler.loop_once();
        CHECK_EQ(completed, 1u);
    }

    TEST_CASE("canceling a task blocked on a semaphore cleans up its queue entry")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{};
        ::verilator_utils::task<void>::promise_type* child_promise{};
        bool parent_caught{};

        const auto child_lambda{
            [&] -> ::verilator_utils::task<void> {
                child_promise = ::std::addressof(
                    (co_await ::verilator_utils::handle<::verilator_utils::task<void>::promise_type>()).promise());
                co_await semaphore.get();
            },
        };
        const auto parent_lambda{
            [&] -> ::verilator_utils::task<void> {
                auto child{co_await ::verilator_utils::to_async(child_lambda())};
                // 让出执行权，使异步子任务挂起在信号量上
                co_await ::verilator_utils::wait_time(1_ps);
                REQUIRE_NE(child_promise, nullptr);
                CHECK(child.cancel_possible());
                child.cancel();
                // 唤醒被取消的等待者：取消请求在恢复点生效，等待者从计数队列中摘除自身
                semaphore.put(1);
                try
                {
                    co_await child;
                }
                catch(const ::verilator_utils::subtask_cancel_exception&)
                {
                    parent_caught = true;
                }
            },
        };

        scheduler.add_task(parent_lambda());
        CHECK_NOTHROW(scheduler.loop_until_finish());
        CHECK(parent_caught);

        // 被取消的等待者已从队列中摘除，信号量的计数语义不受影响
        semaphore.put(1);
        CHECK(semaphore.try_get());
        CHECK_FALSE(semaphore.try_get());
    }

    TEST_CASE("event reclaims the storage of already notified waiters")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::event event{};
        ::std::size_t completed{};

        const auto waiter_lambda{
            [&] -> ::verilator_utils::task<void> {
                co_await event;
                ++completed;
            },
        };

        scheduler.add_task(waiter_lambda());
        scheduler.add_task(waiter_lambda());
        scheduler.loop_once();
        CHECK_EQ(completed, 0u);

        event.notify_one();
        scheduler.loop_once();
        CHECK_EQ(completed, 1u);

        // 回收已被消费的前缀后事件仍然可用
        event.shrink_to_fit();
        event.notify_one();
        scheduler.loop_once();
        CHECK_EQ(completed, 2u);
    }

    TEST_CASE("a semaphore waiter released after the simulation finished cleans up its queue entry")
    {
        scheduler_fixture fixture{};
        auto scheduler{fixture.make_scheduler()};
        ::verilator_utils::semaphore semaphore{};
        bool waiter_resumed{};

        const auto waiter_lambda{
            [&] -> ::verilator_utils::task<void> {
                co_await semaphore.get();
                waiter_resumed = true;
            },
        };

        scheduler.add_task(waiter_lambda());
        scheduler.initial_eval();
        CHECK_FALSE(waiter_resumed);

        // 仿真结束后再释放等待者：恢复点先抛出仿真结束异常，get()的清理分支随之执行
        scheduler.finish();
        semaphore.put(1);
        CHECK_NOTHROW(scheduler.loop_once());
        // 等待者因仿真结束被取消，未继续执行协程体
        CHECK_FALSE(waiter_resumed);
    }

    // NOLINTEND(bugprone-unchecked-optional-access)
}
