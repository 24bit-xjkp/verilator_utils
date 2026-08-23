---
name: cpp-container-iterator-invalidation
description: 'Use when: writing, refactoring, reviewing, or debugging C++ loops that mutate std::vector or other containers through erase, pop_back, push_back, emplace_back, insert, clear, swap-removal, or callbacks. Covers iterator/reference invalidation, cached end iterators, libstdc++ debug iterator assertions, safe index-based traversal, and regression tests.'
argument-hint: 'Describe the container mutation loop or iterator assertion to review'
---

# C++ Container Iterator Invalidation

## When to Use
- Diagnose libstdc++ debug-mode failures mentioning singular, past-the-end, incompatible, or invalid iterators.
- Review loops that modify the container currently being traversed.
- Implement unordered removal with assignment or swap followed by `pop_back()`.
- Fix code that works in release builds or with one standard library but fails with `_GLIBCXX_DEBUG` or hardening enabled.
- Review callbacks invoked during traversal when those callbacks may mutate the same container.

## Core Model
- An iterator, pointer, or reference is valid only while the container operation's invalidation rules preserve it.
- Past-the-end iterators participate in invalidation rules; `end()` is not permanently stable.
- Recomputing `end()` repairs only the cached end iterator. It does not repair the current iterator if that iterator was also invalidated.
- After invalidation, comparing, incrementing, dereferencing, or subtracting the stale iterator is invalid. Debug iterators diagnose many of these operations; release iterators often make the undefined behavior appear to work.
- Different standard-library implementations and build modes may expose the same undefined behavior differently. Portability requires obeying the standard invalidation rules rather than relying on observed pointer values.

## Important `std::vector` Rules
- `pop_back()` invalidates iterators and references to the removed element and the past-the-end iterator.
- `erase(position)` invalidates iterators and references at or after the erased position, including the old `end()`.
- `erase(first, last)` returns the next valid iterator; use that return value instead of incrementing the erased iterator.
- `push_back()`, `emplace_back()`, `insert()`, and capacity growth invalidate every iterator, pointer, and reference if reallocation occurs.
- Without reallocation, vector insertion still invalidates iterators and references at or after the insertion point and invalidates the old `end()`.
- `clear()` invalidates all iterators, pointers, and references to elements.
- Assignment to an existing element does not itself structurally invalidate iterators, but a following structural mutation still can.

## Diagnose a Mutation Loop
1. List every iterator, reference, pointer, span, and cached `end()` derived from the container.
2. List every operation that may structurally mutate the container, including mutations hidden in called functions or callbacks.
3. Apply the exact invalidation rule to each live handle at each mutation point.
4. Inspect all post-mutation operations, including the loop condition and increment expression; these frequently use stale iterators implicitly.
5. Check boundary paths separately: first element, middle element, last element, one-element container, consecutive removals, and removal of every element.
6. Treat library-specific success as insufficient evidence; reproduce with iterator debugging or hardening enabled.

## Safe Traversal Patterns

### Order-Preserving Erase
Use the iterator returned by `erase()`:

```cpp
for(auto iter{items.begin()}; iter != items.end();)
{
    if(should_remove(*iter))
    {
        iter = items.erase(iter);
    }
    else
    {
        ++iter;
    }
}
```

### Unordered Swap Removal
Use an index when order does not matter:

```cpp
for(::std::size_t index{}; index != items.size();)
{
    if(should_remove(items[index]))
    {
        items[index] = ::std::move(items.back());
        items.pop_back();
    }
    else
    {
        ++index;
    }
}
```

- Do not increment `index` after removal; the element moved from the back into the current slot has not been checked yet.
- Self-move assignment may be unsafe for some element types when `index + 1 == items.size()`. If that is possible, branch and skip assignment for the last element, or use a type-specific safe operation.
- Do not use an element reference obtained before `pop_back()` after the call. It may refer to the removed last element.

### Stable Partition and Bulk Erase
When removal order and callback behavior permit, prefer an algorithm that separates selection from structural mutation:

```cpp
auto removed{::std::erase_if(items, should_remove)};
```

- Confirm whether element order, move cost, exception behavior, and callback side effects satisfy the algorithm's requirements.

## Common Broken Pattern

```cpp
for(auto iter{items.begin()}, end{items.end()}; iter != end;)
{
    if(should_remove(*iter))
    {
        *iter = items.back();
        items.pop_back();
        end = items.end();
    }
    else
    {
        ++iter;
    }
}
```

- Recomputing `end` handles only invalidation of the old past-the-end iterator.
- If `iter` pointed to the removed last element, `pop_back()` also invalidated `iter`.
- The next loop-condition comparison then compares a singular iterator with a valid past-the-end iterator, which is invalid.
- In a release implementation, both may contain the same raw address after removal, making the loop appear to terminate correctly by accident.

## Callback and Reentrancy Review
- Assume a callback may mutate the traversed container unless the API contract prevents it.
- A callback that appends may reallocate a vector and invalidate the current element reference before the caller uses it again.
- A callback that erases may invalidate the caller's iterator even if the caller performs no visible mutation.
- Prefer deferred mutation, a separate pending-operation queue, stable identifiers, or a container chosen for the required invalidation guarantees.

## Regression-Test Checklist
- Exercise removal of the only element; this is the shortest path that invalidates both the current iterator and old `end()`.
- Exercise removal of the last element from a multi-element container.
- Exercise consecutive ready/removable elements so the replacement at the current index must be checked again.
- Exercise no removals and removal of all elements.
- Assert semantic results rather than relying only on absence of a crash: all expected elements processed exactly once, no skipped replacement, and final size/emptiness correct.
- Run the narrowest relevant target with libstdc++ iterator debugging or project hardening enabled.

## Validation for This Repository
Use the scheduler test configuration that enables libstdc++ debug iterators while avoiding the sanitizer/reporting conflict:

```bash
xmake f --toolchain=clang -m debug --runtimes=stdc++_shared --use_sanitizer=no --use_std_harden=yes -c
xmake test -v unit_test/scheduler
```

- Also run `git diff --check` after edits.
- If editor diagnostics disagree with the successful compiler result under `_GLIBCXX_DEBUG`, distinguish language-server template-modeling limitations from real build failures before changing unrelated code.

## Review Checklist
- No invalidated iterator, pointer, reference, span, or cached `end()` is used after mutation.
- Iterator-based erase loops assign the return value of `erase()`.
- Unordered vector removal uses an index or another handle that survives `pop_back()`.
- Replacement elements are rechecked after swap removal.
- Last-element and single-element paths are explicitly safe.
- Reallocation-capable operations are not performed while retaining vector element handles.
- Tests run under a mode that actively diagnoses iterator misuse.