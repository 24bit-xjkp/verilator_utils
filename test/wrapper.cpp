module;
#include <doctest_fwd.hpp>
module unit_test;

extern "C++"
{
#include <doctest.h>
}

TEST_SUITE("verilator_utils/wrapper")
{

    TEST_CASE("slice concepts identify wrapper types")
    {
        static_assert(::verilator_utils::is_bit_slice<::verilator_utils::bit_slice<::CData>>);
        static_assert(::verilator_utils::is_bit_slice<::verilator_utils::bit_slice<::SData>>);
        static_assert(::verilator_utils::is_bit_slice<::verilator_utils::bit_slice<::IData>>);
        static_assert(::verilator_utils::is_bit_slice<::verilator_utils::bit_slice<::QData>>);
        static_assert(::verilator_utils::is_bit_slice<::verilator_utils::bit_slice<::VlWide<2>>>);
        static_assert(!::verilator_utils::is_bit_slice<::CData>);
        static_assert(!::verilator_utils::is_bit_slice<const ::verilator_utils::bit_slice<::CData>>);

        static_assert(::verilator_utils::is_vector_slice<::verilator_utils::vector_slice<::CData>>);
        static_assert(::verilator_utils::is_vector_slice<::verilator_utils::vector_slice<::SData>>);
        static_assert(::verilator_utils::is_vector_slice<::verilator_utils::vector_slice<::IData>>);
        static_assert(::verilator_utils::is_vector_slice<::verilator_utils::vector_slice<::CData>>);
        static_assert(::verilator_utils::is_vector_slice<::verilator_utils::vector_slice<::QData>>);
        static_assert(::verilator_utils::is_vector_slice<::verilator_utils::vector_slice<::VlWide<2>>>);
        static_assert(!::verilator_utils::is_vector_slice<::QData>);
        static_assert(!::verilator_utils::is_vector_slice<const ::verilator_utils::vector_slice<::QData>>);

        static_assert(::verilator_utils::is_unpacked_array<::verilator_utils::unpacked_array<::CData, 1>>);
        static_assert(::verilator_utils::is_unpacked_array<::verilator_utils::unpacked_array<::VlWide<2>, 2>>);
        static_assert(!::verilator_utils::is_unpacked_array<::VlUnpacked<::CData, 1>>);
        static_assert(!::verilator_utils::is_unpacked_array<const ::verilator_utils::unpacked_array<::CData, 1>>);

        static_assert(::std::same_as<typename ::verilator_utils::vector_slice<::CData>::cast_type, ::std::uint64_t>);
        static_assert(::std::same_as<typename ::verilator_utils::vector_slice<::SData>::cast_type, ::std::uint64_t>);
        static_assert(::std::same_as<typename ::verilator_utils::vector_slice<::IData>::cast_type, ::std::uint64_t>);
        static_assert(::std::same_as<typename ::verilator_utils::vector_slice<::QData>::cast_type, ::std::uint64_t>);
        static_assert(::std::same_as<typename ::verilator_utils::vector_slice<::VlWide<2>>::cast_type, ::VlWide<2>>);
    }

    TEST_CASE("bit slice reads writes and formats a single bit")
    {
        ::CData data{0b1010'1010u};
        ::verilator_utils::bit_slice<::CData> bit_one{data, 1};
        ::verilator_utils::bit_slice<::CData> bit_two{data, 2};

        CHECK_EQ(static_cast<::std::uint64_t>(bit_one), 1u);
        CHECK_EQ(static_cast<std::uint64_t>(bit_two), 0u);
        CHECK_EQ(bit_one.width(), 1u);
        CHECK_EQ(bit_one.to_string(), "0x1");
        CHECK_EQ(bit_two.to_string(), "0x0");

        bit_one = 0;
        CHECK_EQ(data, 0b1010'1000u);
        CHECK_EQ(static_cast<::std::uint64_t>(bit_one), 0u);

        bit_two = 1;
        CHECK_EQ(data, 0b1010'1100u);
        CHECK_EQ(static_cast<std::uint64_t>(bit_two), 1u);
    }

    TEST_CASE("bit slice reads and writes bits in every scalar data type")
    {
        ::SData sdata{0x00ffu};
        ::IData idata{0xffff'0000u};
        ::QData qdata{0x0000'0001'0000'0000u};

        ::verilator_utils::bit_slice<::SData> sdata_bit{sdata, 8};
        ::verilator_utils::bit_slice<::IData> idata_bit{idata, 16};
        ::verilator_utils::bit_slice<::QData> qdata_bit{qdata, 32};

        CHECK_EQ(static_cast<::std::uint64_t>(sdata_bit), 0u);
        sdata_bit = 1;
        CHECK_EQ(sdata, 0x01ffu);
        CHECK_EQ(static_cast<::std::uint64_t>(sdata_bit), 1u);

        CHECK_EQ(static_cast<::std::uint64_t>(idata_bit), 1u);
        idata_bit = 0;
        CHECK_EQ(idata, 0xfffe'0000u);
        CHECK_EQ(static_cast<::std::uint64_t>(idata_bit), 0u);

        CHECK_EQ(static_cast<::std::uint64_t>(qdata_bit), 1u);
        qdata_bit = 0;
        CHECK_EQ(qdata, 0u);
        CHECK_EQ(qdata_bit.to_string(), "0x0");
    }

    TEST_CASE("bit slice reads and writes wide data word boundaries")
    {
        ::VlWide<2> data{0x0000'0000u, 0x0000'0002u};
        ::verilator_utils::bit_slice<::VlWide<2>> bit_thirty_one{data, 31};
        ::verilator_utils::bit_slice<::VlWide<2>> bit_thirty_two{data, 32};
        ::verilator_utils::bit_slice<::VlWide<2>> bit_thirty_three{data, 33};

        CHECK_EQ(static_cast<::std::uint64_t>(bit_thirty_one), 0u);
        CHECK_EQ(static_cast<::std::uint64_t>(bit_thirty_two), 0u);
        CHECK_EQ(static_cast<::std::uint64_t>(bit_thirty_three), 1u);

        bit_thirty_one = 1;
        bit_thirty_two = 1;
        bit_thirty_three = 0;

        CHECK_EQ(data.at(0), 0x8000'0000u);
        CHECK_EQ(data.at(1), 0x0000'0001u);
        CHECK_EQ(bit_thirty_one.to_string(), "0x1");
        CHECK_EQ(bit_thirty_three.to_string(), "0x0");
    }

    TEST_CASE("vector slice reads scalar ranges and individual bits")
    {
        ::IData data{0x1234'5678u};
        ::verilator_utils::vector_slice<::IData> byte_slice{data, 15, 8};
        ::verilator_utils::vector_slice<::IData> low_nibble{data, 3, 0};

        CHECK_EQ(byte_slice.width(), 8u);
        CHECK_EQ(static_cast<::std::uint64_t>(byte_slice), 0x56u);
        CHECK_EQ(byte_slice.to_string(), "0x56");
        CHECK_EQ(static_cast<::std::uint64_t>(low_nibble), 0x8u);
        CHECK_EQ(low_nibble.to_string(), "0x8");

        CHECK_EQ(static_cast<::std::uint64_t>(byte_slice[8]), 0u);
        CHECK_EQ(static_cast<::std::uint64_t>(byte_slice[9]), 1u);
        CHECK_EQ(static_cast<::std::uint64_t>(byte_slice[11, 8]), 0x6u);
    }

    TEST_CASE("vector slice reads ranges from every scalar data type")
    {
        ::CData cdata{0xabu};
        ::SData sdata{0x12abu};
        ::QData qdata{0x0123'4567'89ab'cdefu};

        ::verilator_utils::vector_slice<::CData> cdata_slice{cdata, 7, 4};
        ::verilator_utils::vector_slice<::SData> sdata_slice{sdata, 11, 4};
        ::verilator_utils::vector_slice<::QData> qdata_slice{qdata, 39, 8};

        CHECK_EQ(cdata_slice.width(), 4u);
        CHECK_EQ(static_cast<::std::uint64_t>(cdata_slice), 0xau);
        CHECK_EQ(cdata_slice.to_string(), "0xa");
        CHECK_EQ(static_cast<::std::uint64_t>(sdata_slice), 0x2au);
        CHECK_EQ(sdata_slice.to_string(), "0x2a");
        CHECK_EQ(static_cast<::std::uint64_t>(qdata_slice), 0x67'89ab'cdu);
        CHECK_EQ(qdata_slice.to_string(), "0x6789abcd");
    }

    TEST_CASE("vector slice reads wide ranges across words")
    {
        ::VlWide<3> data{0x89ab'cdefu, 0x0123'4567u, 0x0000'00f0u};
        ::verilator_utils::vector_slice<::VlWide<3>> full_slice{data, 72};
        ::verilator_utils::vector_slice<::VlWide<3>> cross_word_slice{data, 39, 28};
        ::verilator_utils::vector_slice<::VlWide<3>> high_slice{data, 71, 64};

        auto full_value{static_cast<::VlWide<3>>(full_slice)};
        auto cross_word_value{static_cast<::VlWide<3>>(cross_word_slice)};
        auto high_value{static_cast<::VlWide<3>>(high_slice)};

        CHECK_EQ(full_slice.width(), 72u);
        CHECK_EQ(full_slice.to_string(), "0xf00123456789abcdef");
        CHECK_EQ(full_value.at(0), 0x89ab'cdefu);
        CHECK_EQ(full_value.at(1), 0x0123'4567u);
        CHECK_EQ(full_value.at(2), 0x0000'00f0u);
        CHECK_EQ(cross_word_value.at(0), 0x678u);
        CHECK_EQ(cross_word_value.at(1), 0u);
        CHECK_EQ(cross_word_slice.to_string(), "0x678");
        CHECK_EQ(high_value.at(0), 0xf0u);
        CHECK_EQ(high_slice.to_string(), "0xf0");
        CHECK_EQ(static_cast<::std::uint64_t>(full_slice[32]), 1u);
        CHECK_EQ(static_cast<::VlWide<3>>(full_slice[35, 32]).at(0), 0x7u);
    }

    TEST_CASE("vector slice assigns scalar values without touching surrounding bits")
    {
        ::IData data{0xffff'0000u};
        ::verilator_utils::vector_slice<::IData> byte_slice{data, 15, 8};

        byte_slice = 0xabu;
        CHECK_EQ(data, 0xffff'ab00zu);
        CHECK_EQ(static_cast<::std::uint64_t>(byte_slice), 0xabu);
        CHECK_EQ(byte_slice, 0xabzu);
        CHECK_EQ(byte_slice.to_string(), "0xab");

        byte_slice = 0x00u;
        CHECK_EQ(data, 0xffff'0000u);
        CHECK_EQ(byte_slice, 0x00zu);
    }

    TEST_CASE("vector slice assigns scalar values up to sixty four bits")
    {
        ::QData data{0xffff'ffff'0000'0000u};
        ::verilator_utils::vector_slice<::QData> lower_bits{data, 47, 16};

        lower_bits = 0x89ab'cdefu;
        CHECK_EQ(data, 0xffff'89ab'cdef'0000u);
        CHECK_EQ(lower_bits, 0x89ab'cdefzu);
        CHECK_EQ(lower_bits.to_string(), "0x89abcdef");

        ::verilator_utils::vector_slice<::QData> full_value{data, 64};
        full_value = 0x0123'4567'89ab'cdefu;
        CHECK_EQ(data, 0x0123'4567'89ab'cdefu);
        CHECK_EQ(full_value, 0x0123'4567'89ab'cdefzu);
    }

    TEST_CASE("vector slice assigns wide value without touching surrounding bits")
    {
        ::VlWide<3> data{0xffff'ffffu, 0xffff'ffffu, 0x0000'00ffu};
        ::VlWide<3> value{0x89ab'cdefu, 0x0000'0123u, 0u};
        ::verilator_utils::vector_slice<::VlWide<3>> middle_bits{data, 67, 12};

        middle_bits = value;

        CHECK_EQ(data.at(0), 0xbcde'ffffu);
        CHECK_EQ(data.at(1), 0x0012'389au);
        CHECK_EQ(data.at(2), 0x0000'00f0u);
        CHECK_EQ(middle_bits.to_string(), "0x00012389abcdef");
    }

    TEST_CASE("vector slice assigns from another scalar slice")
    {
        ::IData destination{0xffff'0000u};
        ::SData source{0x12abu};
        ::verilator_utils::vector_slice<::IData> destination_byte{destination, 15, 8};
        ::verilator_utils::vector_slice<::SData> source_byte{source, 7, 0};

        destination_byte = source_byte;
        CHECK_EQ(destination, 0xffff'ab00u);
        CHECK_EQ(destination_byte, source_byte);
    }

    TEST_CASE("vector slice assigns scalar slice into wide slice")
    {
        ::VlWide<2> destination{0xffff'ffffu, 0xffff'ffffu};
        ::QData source{0x0000'0123'89ab'cdefu};
        ::verilator_utils::vector_slice<::VlWide<2>> destination_bits{destination, 55, 8};
        ::verilator_utils::vector_slice<::QData> source_bits{source, 47, 0};

        destination_bits = source_bits;

        CHECK_EQ(destination.at(0), 0xabcd'efffu);
        CHECK_EQ(destination.at(1), 0xff01'2389u);
        CHECK_EQ(destination_bits, source_bits);
        CHECK_EQ(destination_bits.to_string(), "0x012389abcdef");
    }

    TEST_CASE("vector slice assigns wide slice into scalar slice")
    {
        ::QData destination{0xffff'ffff'ffff'ffffu};
        ::VlWide<2> source{0x89ab'cdefu, 0x0000'0123u};
        ::verilator_utils::vector_slice<::QData> destination_bits{destination, 47, 0};
        ::verilator_utils::vector_slice<::VlWide<2>> source_bits{source, 47, 0};

        destination_bits = source_bits;

        CHECK_EQ(destination, 0xffff'0123'89ab'cdefu);
        CHECK_EQ(destination_bits, source_bits);
        CHECK_EQ(destination_bits.to_string(), "0x012389abcdef");
    }

    TEST_CASE("vector slice compares against hex wrapper and std format")
    {
        ::IData scalar_data{0x0000'0abcu};
        ::VlWide<2> wide_data{0x89ab'cdefu, 0x0000'0123u};
        ::verilator_utils::vector_slice<::IData> scalar_slice{scalar_data, 11, 0};
        ::verilator_utils::vector_slice<::VlWide<2>> wide_slice{wide_data, 47, 0};

        CHECK_EQ(scalar_slice, ::verilator_utils::hex_wrapper_t{0xabc, 12});
        CHECK_EQ(wide_slice, ::verilator_utils::hex_wrapper_t{0x0123'89ab'cdef, 48});
        CHECK_EQ(::std::format("{}", scalar_slice), "0xabc");
        CHECK_EQ(::std::format("{}", wide_slice), "0x012389abcdef");
        CHECK_EQ(::std::format("{}", ::verilator_utils::bit_slice<::IData>{scalar_data, 11}), "0x1");
    }

    TEST_CASE("unpacked array exposes sliced elements as span and supports assignment")
    {
        ::VlUnpacked<::CData, 3> data{};
        data[0] = 0x10u;
        data[1] = 0x20u;
        data[2] = 0x30u;
        ::verilator_utils::unpacked_array<::CData, 3> wrapper{data, 8};

        CHECK_EQ(wrapper.size(), 3u);
        CHECK_EQ(wrapper.width(), 8u);
        CHECK_EQ(static_cast<::std::uint64_t>(wrapper[0]), 0x10u);
        CHECK_EQ(static_cast<::std::uint64_t>(wrapper[1]), 0x20u);
        CHECK_EQ(static_cast<::std::uint64_t>(wrapper[2]), 0x30u);

        ::std::array<::std::uint64_t, 3> values{0xabu, 0xcdu, 0xefu};
        wrapper = ::std::span<::std::uint64_t, 3>{values};

        CHECK_EQ(data[0], 0xabu);
        CHECK_EQ(data[1], 0xcdu);
        CHECK_EQ(data[2], 0xefu);
        CHECK_EQ(wrapper.span().size(), 3u);
        CHECK_EQ(wrapper, ::std::span<::std::uint64_t, 3>{values});
        CHECK_EQ(wrapper.to_string(), "[0xab, 0xcd, 0xef]");
        static_assert(::std::formattable<verilator_utils::vector_slice<unsigned char>, char>);
        CHECK_EQ(::std::format("{}", wrapper), "[0xab, 0xcd, 0xef]");
    }

    TEST_CASE("unpacked array supports wide elements and wrapper copy")
    {
        ::VlUnpacked<::VlWide<2>, 2> destination_data{};
        ::VlUnpacked<::VlWide<2>, 2> source_data{};
        source_data[0] = ::VlWide<2>{0x89ab'cdefu, 0x0000'0123u};
        source_data[1] = ::VlWide<2>{0x7654'3210u, 0x0000'fedcu};
        ::verilator_utils::unpacked_array<::VlWide<2>, 2> destination{destination_data, 48};
        ::verilator_utils::unpacked_array<::VlWide<2>, 2> source{source_data, 48};

        destination = source;

        CHECK_EQ(destination_data[0].at(0), 0x89ab'cdefu);
        CHECK_EQ(destination_data[0].at(1), 0x0000'0123u);
        CHECK_EQ(destination_data[1].at(0), 0x7654'3210u);
        CHECK_EQ(destination_data[1].at(1), 0x0000'fedcu);
        CHECK_EQ(destination, source);
        CHECK_EQ(destination.to_string(), "[0x012389abcdef, 0xfedc76543210]");
        CHECK_EQ(::std::format("{}", destination), "[0x012389abcdef, 0xfedc76543210]");
    }

    TEST_CASE("make unpacked array recursively wraps multidimensional arrays")
    {
        ::VlUnpacked<::CData, 4> one_dimensional_data{};
        auto one_dimensional_wrapper{::verilator_utils::make_unpacked_array(one_dimensional_data, 8)};
        static_assert(::std::same_as<decltype(one_dimensional_wrapper), ::verilator_utils::unpacked_array<::CData, 4>>);

        ::VlUnpacked<::VlUnpacked<::VlUnpacked<::CData, 4>, 3>, 2> data{};
        data[0][0][0] = 0x12u;
        data[1][2][3] = 0x34u;

        auto wrapper{::verilator_utils::make_unpacked_array(data, 8)};
        using expected_type = ::std::array<::std::array<::verilator_utils::unpacked_array<::CData, 4>, 3>, 2>;
        static_assert(::std::same_as<decltype(wrapper), expected_type>);

        CHECK_EQ(wrapper.size(), 2u);
        CHECK_EQ(wrapper[0].size(), 3u);
        CHECK_EQ(wrapper[1][2].size(), 4u);
        CHECK_EQ(wrapper[1][2].width(), 8u);
        CHECK_EQ(static_cast<::std::uint64_t>(wrapper[0][0][0]), 0x12u);
        CHECK_EQ(static_cast<::std::uint64_t>(wrapper[1][2][3]), 0x34u);

        wrapper[1][2][3] = 0x5au;

        CHECK_EQ(data[1][2][3], 0x5au);
        CHECK_EQ(static_cast<::std::uint64_t>(wrapper[1][2][3]), 0x5au);

        ::VlUnpacked<::VlUnpacked<::CData, 2>, 2> data2{
            {{1, 2}, {3, 4}}
        };
        auto wrapper2{::verilator_utils::make_unpacked_array(data2, 4)};
        CHECK_EQ(::std::format("{}", wrapper2), "[[0x1, 0x2], [0x3, 0x4]]");
    }
}
