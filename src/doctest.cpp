module;
#include "doctest_fwd.hpp"
#define DOCTEST_PARTS_PUBLIC_STD_TYPE_TRAITS
export module verilator_utils.doctest;
import std;
import std.compat;

export namespace doctest::detail
{
    namespace types = ::std;
}

extern "C++"
{
#include <doctest.h>
}

export namespace doctest
{
    using ::doctest::Approx;
    using ::doctest::Context;
    using ::doctest::getContextOptions;
    using ::doctest::String;
    using ::doctest::StringMaker;

    namespace assertType
    {
        using ::doctest::assertType::Enum;
    }

    namespace detail
    {

        namespace binaryAssertComparison
        {
            using ::doctest::detail::binaryAssertComparison::Enum;
        }

        using ::doctest::detail::acquireGeneratorValue;
        using ::doctest::detail::ExpressionDecomposer;
        using ::doctest::detail::MakeContextScope;
        using ::doctest::detail::MessageBuilder;
        using ::doctest::detail::regTest;
        using ::doctest::detail::ResultBuilder;
        using ::doctest::detail::TestCase;
        using ::doctest::detail::TestSuite;
    }  // namespace detail
}  // namespace doctest
