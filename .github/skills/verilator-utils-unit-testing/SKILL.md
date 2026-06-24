---
name: verilator-utils-unit-testing
description: 'Use when: writing, refactoring, or reviewing unit tests for the verilator_utils C++ module project. Covers xmake test registration, doctest assertion style, Verilator data edge cases, and project test layout.'
argument-hint: 'Describe the component or behavior to test'
---

# verilator_utils Unit Testing

## When to Use
- Add new C++ unit tests under `test/`.
- Refactor existing doctest tests in this repository.
- Add regression tests for utilities, scheduler behavior, wrapper behavior, or Verilator-backed RTL helpers.
- Decide how to run a specific test with `xmake`.

## Project Test Layout
- Root `xmake.lua` enables `c++latest`, requires `doctest` and `verilator`, enables exceptions, and includes all nested `xmake.lua` files.
- `src/xmake.lua` builds `verilator_utils` as a static C++20 module library from `src/*.cpp`, excluding `src/main.cpp`.
- `test/main.cpp` defines `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`; test files must include `<doctest.h>` but must not define another doctest main.
- `test/xmake.lua` defines `unit_test_main` as an object target for `test/main.cpp`.
- `test/xmake.lua` defines `unit_test`, depends on `verilator_utils` and `unit_test_main`, enables C++ modules, and includes `*.cpp|rtl_*.cpp|main.cpp`.
- Each file matched by `test/xmake.lua` is registered as a test named by its basename, with run args `-ts=verilator_utils/<basename>`.
- Practical example: `test/utils.cpp` maps to xmake test pattern `unit_test/utils` and doctest suite `verilator_utils/utils`.

## xmake Commands
- Run a specific test after edits: `xmake test unit_test/<basename>`.
- Run the utils tests: `xmake test unit_test/utils`.
- Run all tests registered under the target: `xmake test unit_test/*`.
- Use `-r` when build failed due to BMI/object files after changing C++ modules.
- If a command exits with no useful output, check `xmake test --help` and verify the registered test pattern.
- Use `xmake test -v unit_test/<basename>` to see the detail output of the test case.
- Use `xmake run unit_test` to run the unit test program directly.

## doctest Style
- Prefer doctest binary checks over boolean expressions:
  - Use `CHECK_EQ(actual, expected)` instead of `CHECK(actual == expected)`.
  - Use `CHECK_NE`, `CHECK_LT`, `CHECK_LE`, `CHECK_GT`, or `CHECK_GE` for comparisons.
  - Use `REQUIRE_*` only when the rest of the test cannot safely continue.
- For custom diagnostic context, use `CAPTURE(value)` near the assertion or `CHECK_MESSAGE(condition, message)` when a domain-specific message is clearer.
- Keep one `TEST_SUITE` per logical component, usually matching `verilator_utils/<basename>` for utility tests.
- Split behavior into small `TEST_CASE`s with precise English names.
- Use `static_assert` for compile-time concepts, type aliases, and module API contracts.
- Preserve repository style: global names use `::`, imports use C++ modules (`import verilator_utils; import std;`), and local constants prefer brace initialization.

## Edge-Case Checklist
- Numeric conversions: test zero, minimum non-zero value, fractional truncation, and unit-boundary conversions.
- Arithmetic wrappers: test identity cases, zero operands, truncating division/multiplication, equality, and ordering.
- Formatting helpers: test widths around hex digit boundaries such as `1`, `4`, `5`, `8`, `16`, `32`, and word-aligned widths.
- Verilator scalar data: cover `CData`, `SData`, `IData`, and `QData` when testing type traits or formatting.
- Verilator wide data: cover `VlWide<1>`, multi-word `VlWide<N>`, partial top words, and word-aligned widths.
- Type traits: include negative cases such as ordinary C++ types and cv-qualified variants if the concept intentionally rejects them.
- RTL/scheduler tests: cover reset behavior, first active edge after reset, synchronous transitions, asynchronous pulses, and any queued expected-output exhaustion.

## Workflow
1. Inspect the component API and existing adjacent tests before editing.
2. Add or update tests in the smallest relevant `test/<basename>.cpp` file.
3. Prefer deterministic values with obvious expected results.
4. Add edge cases near the normal behavior they validate.
5. Run `xmake test unit_test/<basename>`.
6. Fix only failures related to the changed test or target behavior.
7. Check diagnostics for the edited file before finishing.

## Example Patterns
- Utility test includes normally start with:
  - `#include <doctest.h>`
  - Any required Verilator headers such as `#include <verilated.h>`
  - `import verilator_utils;`
  - `import std;`
- Use `using namespace ::verilator_utils::literals;` inside a suite when testing time literals.
- For string formatting checks, compare exact strings with `CHECK_EQ(value.to_string(), "expected")`.
- For wide Verilator data, call helpers directly when the public wrapper only stores scalar values.
