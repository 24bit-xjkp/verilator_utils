---
name: cpp-coroutine-lambda-safety
description: 'Use when: writing, refactoring, or reviewing C++ coroutine lambdas, especially lambdas returning task/async_task or using co_await/co_return. Covers avoiding coroutine lambda captures, moving state into parameters, and validating scheduler-style tests.'
argument-hint: 'Describe the coroutine lambda or task code to review/refactor'
---

# C++ Coroutine Lambda Safety

## When to Use
- Refactor C++ coroutine lambdas that use a capture list, such as `[&]`, `[=]`, or `[foo]`.
- Review code returning coroutine task types, especially `::verilator_utils::task` or related async wrappers.
- Fix sanitizer reports that may come from coroutine lifetime issues, such as use-after-free or stack-use-after-scope.
- Add or update scheduler tests that create coroutine tasks from local variables.

## Core Rule
- Do not rely on a coroutine lambda capture list for values that must survive after the lambda call returns.
- C++ coroutine state is stored in the coroutine frame, but lambda captures live in the lambda closure object.
- If the closure object is a temporary, captured references or values can become invalid while the coroutine is still suspended.
- Put all coroutine dependencies in the lambda parameter list so they are stored as coroutine parameters.

## Refactoring Procedure
1. Find lambdas whose body contains `co_await`, `co_return`, or `co_yield`, or whose trailing return type is a coroutine task type.
2. Treat any non-empty capture list on those lambdas as unsafe unless the coroutine is proven never to suspend.
3. Replace the capture list with `[]`.
4. Add explicit parameters for every object previously captured and used across the coroutine body.
5. Pass arguments at the immediately invoked call site, usually by reference for test-owned state and scheduler objects.
6. Preserve ordinary nested non-coroutine callbacks when they are consumed synchronously by an awaiter, but check their lifetime separately.
7. Re-run diagnostics and the narrowest relevant test target.

## Preferred Pattern
- Use explicit reference parameters for local test state that outlives the async task.
- Keep parameter order stable and readable: scheduler/context first, signal or task objects next, output flags/vectors last.
- Use fully qualified project types to match repository style, for example `::verilator_utils::eval_scheduler&`.
- Keep immediate invocation at the original construction site so the surrounding test flow remains unchanged.

## Review Checklist
- No coroutine lambda uses `[&]`, `[=]`, or named captures.
- Every variable used by the coroutine body is either local to the coroutine body or appears in the parameter list.
- `async_task` or task owner objects outlive any coroutine that awaits them.
- Scheduler and DUT/context objects outlive tasks queued into the scheduler.
- Sanitizer failures after the refactor are checked for independent task ownership or destruction-order bugs rather than assumed to be capture-list issues.

## Validation
- Search the edited files for coroutine lambdas with non-empty captures.
- Check editor/compiler diagnostics for missing parameters after removing captures.
- For this repository, run the narrow test target when appropriate, such as `xmake test -r unit_test/scheduler`.
- If AddressSanitizer still reports use-after-free, inspect coroutine handle ownership and queued tasks before expanding the refactor scope.
