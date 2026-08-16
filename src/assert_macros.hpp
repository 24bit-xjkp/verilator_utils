#pragma once

// NOLINTBEGIN(cppcoreguidelines-macro-usage)

/**
 * @brief 断言检查宏，自动捕获调用处的源代码位置
 *
 * 展开为verilator_utils::check函数调用
 * @param condition 断言条件
 * @param ... 可选，格式化字符串和格式化参数
 * @code {.cpp}
 * VU_CHECK(width != 0, "数据宽度不能为0，实际为{}", width);
 * @endcode
 */
#define VU_CHECK(condition, ...)                                                                                                 \
    ::verilator_utils::check((condition), ::std::source_location::current() __VA_OPT__(, ) __VA_ARGS__)

// NOLINTEND(cppcoreguidelines-macro-usage)
