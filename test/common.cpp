module;
#include <verilated_config.h>
export module unit_test;
export import verilator_utils.full;

export {
    using namespace ::verilator_utils;
    using namespace ::verilator_utils::verilator;
    using namespace ::std::string_view_literals;
}

struct verilator_version_t
{
    /// 主版本号
    ::std::size_t major{};
    /// 次版本号
    ::std::size_t minor{};

    /**
     * @brief 解析Verilator版本字符串
     *
     * @param version 版本字符串，如5.052
     * @code {.cpp}
     * constexpr auto [major, minor]{verilator_version_t{"5.052"sv}};
     * static_assert(major == 5 && minor == 52);
     * @endcode
     */
    constexpr explicit verilator_version_t(::std::string_view version)
    {
        const char* iter{version.begin()};
        const char* end{version.end()};
        if(auto [new_iter, ec]{::std::from_chars(iter, end, major)};
           // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization)
           ec != ::std::errc{} || *new_iter++ != '.')
        {
            throw ::std::invalid_argument("版本解析失败");
        }
        else
        {
            iter = new_iter;
        }
        if(!::std::from_chars(iter, end, minor)) { throw ::std::invalid_argument("版本解析失败"); }
    }

    constexpr friend bool operator== (verilator_version_t lhs, verilator_version_t rhs) noexcept = default;
    constexpr friend auto operator<=> (verilator_version_t lhs, verilator_version_t rhs) noexcept = default;
};

template <verilator_version_t version>
struct dummy_verilated_model : ::VerilatedModel
{
    explicit dummy_verilated_model(::VerilatedContext& context) : ::VerilatedModel{context} {}
};

/// 提供一个与VerilatedModel构造函数兼容的空基类
struct dummy_verilated_model_base
{
    explicit dummy_verilated_model_base(::VerilatedContext& /* unused */) noexcept {}
};

/// 将基类选择推迟到模板实例化时，以便重写一些可能不存在的虚函数
template <bool satisfy>
using select_base = ::std::conditional_t<satisfy, ::VerilatedModel, ::dummy_verilated_model_base>;

constexpr verilator_version_t v5_052{"5.052"sv};

template <verilator_version_t version>
    requires (version >= ::v5_052)
struct dummy_verilated_model<version> : ::select_base<version >= ::v5_052>
{
    explicit dummy_verilated_model(::VerilatedContext& context) : ::select_base<version >= ::v5_052>{context} {}

    // Verilator v5.052新增了如下纯虚函数，为其添加默认实现

private:
    void evalBegin() override {}

    void evalEnd() override {}

    void evalStatic() override {}

    void evalInitial() override {}

    void evalSample() override {}

    bool evalStl(bool /* unused */) override { return false; }

    bool evalIco(bool /* unused */) override { return false; }

    bool evalAct() override { return false; }

    bool evalInact() override { return false; }

    bool evalNba() override { return false; }

    bool evalObs() override { return false; }

    bool evalReact() override { return false; }

    void evalPostponed() override {}

    void dumpTriggersStl() override {}

    void dumpTriggersIco() override {}

    void dumpTriggersAct() override {}

    void dumpTriggersNba() override {}

    void dumpTriggersObs() override {}

    void dumpTriggersReact() override {}

    void evalFinal() override {}
};

export {
    constexpr verilator_version_t verilator_version{VERILATOR_VERSION};

    struct fake_dut : ::dummy_verilated_model<::verilator_version>
    {
        explicit fake_dut(::VerilatedContext& context) : ::dummy_verilated_model<::verilator_version>{context} {}

        void eval() {}

        [[nodiscard]] const char* hierName() const override { return "fake_dut"; }

        [[nodiscard]] const char* modelName() const override { return "fake_dut"; }

        [[nodiscard]] unsigned threads() const override { return 1u; }

        void prepareClone() const { contextp()->prepareClone(); }

        void atClone() const { contextp()->threadPoolpOnClone(); }
    };
}
