export module verilator_utils:internal;
import std;

namespace verilator_utils
{
    namespace detail
    {
        /**
         * @brief 判断给定类型type是否在模板参数包types...中出现一次
         *
         * @tparam type 要查找的类型
         * @tparam types 模板参数包
         * @return type是否在types...中出现一次
         */
        template <typename type, typename... types>
        inline consteval bool find_once_in_types() noexcept
        {
            return []<::std::size_t... indexes>(::std::index_sequence<indexes...> /* unused */) static consteval noexcept
            {
                return ((::std::same_as<type, types...[indexes]> ? 1 : 0) + ...);
            }(::std::make_index_sequence<sizeof...(types)>{}) == 1;
        }

        template <typename type>
        constexpr inline bool is_variant_impl{};

        template <typename... types>
        constexpr inline bool is_variant_impl<::std::variant<types...>>{true};

        template <typename type, typename type2>
        struct variant_index_impl
        {
            static_assert(::verilator_utils::detail::is_variant_impl<type2>, "必须为std::variant类型");
        };

        template <typename type, typename... types>
            requires (::verilator_utils::detail::find_once_in_types<type, types...>()) // NOLINT(readability-redundant-parentheses)
        struct variant_index_impl<type, ::std::variant<types...>> :
            ::std::integral_constant<::std::size_t,
                                     []<::std::size_t... indexes>(
                                         ::std::index_sequence<indexes...> /* unused */) static consteval noexcept
                                     {
                                         return ((::std::same_as<type, types...[indexes]> ? indexes : 0) + ...);
                                     }(::std::make_index_sequence<sizeof...(types)>{})>
        {
        };

        template <typename type, typename... types>
        struct variant_index_impl<type, ::std::variant<types...>>
        {
            static_assert(::verilator_utils::detail::find_once_in_types<type, types...>(),
                          "给定类型type在模板参数包types...中出现次数不为1");
        };
    }  // namespace detail

    /**
     * @brief 判断给定类型type是否为std::variant
     *
     * @tparam type 要判断的类型
     */
    template<typename type>
    concept is_variant = ::verilator_utils::detail::is_variant_impl<type>;

    /**
     * @brief 给定类型type在std::variant模板参数包中的索引
     *
     * @tparam type 要查找的类型
     * @tparam type在std::variant模板参数包中的索引
     */
    template <typename type, ::verilator_utils::is_variant variant>
    constexpr inline ::std::size_t variant_type_index{::verilator_utils::detail::variant_index_impl<type, variant>::value};
}  // namespace verilator_utils
