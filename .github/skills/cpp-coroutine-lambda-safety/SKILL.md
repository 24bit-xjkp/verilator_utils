---
name: cpp-coroutine-lambda-safety
description: 'Use when: writing, refactoring, or reviewing C++ coroutine lambdas, especially lambdas returning task/async_task or using co_await/co_return. Covers explicit object parameters, safe closure lifetime, capture lifetime boundaries, and scheduler-style test validation.'
argument-hint: 'Describe the coroutine lambda or task code to review/refactor'
---

# C++ Coroutine Lambda Safety

## When to Use
- Refactor C++ coroutine lambdas that use a capture list, such as `[&]`, `[=]`, or `[foo]`.
- Review code returning coroutine task types, especially `::verilator_utils::task` or related async wrappers.
- Fix sanitizer reports that may come from coroutine lifetime issues, such as use-after-free or stack-use-after-scope.
- Add or update scheduler tests that create coroutine tasks from local variables.

## Core Rule
- C++ coroutine state is stored in the coroutine frame, but lambda captures live in the lambda closure object.
- A normal coroutine lambda member function receives the closure through an implicit `this` pointer; the closure itself is not copied into the coroutine frame.
- Declare a by-value explicit object parameter, `this auto`, so invoking the lambda copies the closure into a coroutine parameter and therefore into the coroutine frame.
- Copying the closure preserves captured values and reference members, but does not extend the lifetime of objects captured by reference.
- Every referenced object must still outlive the coroutine task.

## Refactoring Procedure
1. Find lambdas whose body contains `co_await`, `co_return`, or `co_yield`, or whose trailing return type is a coroutine task type.
2. Add `this auto` as the first lambda parameter, including captureless and immediately invoked coroutine lambdas.
3. For local test state with a lifetime covering task execution, use `[&](this auto, ...)` and remove parameters that only forwarded those same locals.
4. Keep true per-invocation inputs as ordinary parameters after `this auto`, such as a task ID or expected value.
5. Use `[](this auto, ...)` when the coroutine does not access enclosing state.
6. Update every call site after removing forwarded parameters.
7. Preserve nested non-coroutine predicates and callbacks unless their own lifetime is unsafe.
8. Re-run diagnostics, search for missed coroutine lambdas, and execute the narrowest relevant test target.

## Preferred Patterns

Captureless immediately invoked coroutine:

```cpp
auto task{[](this auto) -> ::verilator_utils::task
		  {
			  co_await ::verilator_utils::wait_time(1_ps);
		  }()};
```

Coroutine using test-owned local state:

```cpp
auto task{[&](this auto) -> ::verilator_utils::task
		  {
			  co_await ::verilator_utils::wait_time(1_ps);
			  observed_time = scheduler.time_in_time_precision();
		  }()};
```

Reusable coroutine lambda with a per-call value:

```cpp
const auto make_task{[&](this auto, int task_id) -> ::verilator_utils::task
					 {
						 co_await ::verilator_utils::wait_event([&event_ready] { return event_ready; });
						 resumed_tasks.push_back(task_id);
					 }};

auto first{make_task(1)};
auto second{make_task(2)};
```

## Lifetime Boundaries
- `[&](this auto)` copies a closure containing references; it prevents a dangling closure pointer, not dangling referenced objects.
- `[=](this auto)` copies captured values into the closure and then copies that closure into the coroutine frame; check whether copying is intended and supported.
- Do not use `this auto&` or `this auto&&` for this purpose because they keep a reference to the original closure rather than storing a closure copy in the frame.
- Ensure the scheduler, context, DUT, child tasks, output containers, and flags outlive every task that captures them by reference.
- Keep immediate invocation at the original construction site when possible so task ownership and scheduling order stay unchanged.

## Review Checklist
- Every lambda coroutine has a by-value `this auto` explicit object parameter.
- Captureless coroutine lambdas use `[](this auto, ...)`.
- Capturing coroutine lambdas use `[...](this auto, ...)`, and every referenced object has a proven enclosing lifetime.
- Parameters retained after `this auto` represent genuine per-call inputs rather than repeated forwarding of captured locals.
- All call sites match the simplified parameter list.
- `async_task` or task owner objects outlive any coroutine that awaits them.
- Scheduler and DUT/context objects outlive tasks queued into the scheduler.
- Sanitizer failures after the refactor are checked for independent task ownership or destruction-order bugs rather than assumed to be capture-list issues.

## Validation
- Search the edited files for coroutine lambdas that still lack `this auto`.
- Check editor/compiler diagnostics for missing parameters after removing captures.
- For this repository, run the narrow test target when appropriate, such as `xmake test -r unit_test/scheduler`.
- If AddressSanitizer still reports use-after-free, inspect coroutine handle ownership and queued tasks before expanding the refactor scope.
