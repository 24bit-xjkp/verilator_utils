---
name: verilator-utils-code-review
description: 'Use when: reviewing, auditing, or assessing pull requests and code changes in the verilator_utils C++26/C++20-module project, including src framework code, scheduler/coroutine lifetimes, Verilator wrappers and context, xmake integration, hand-written RTL integration tests such as test/rtl_edge_detector.cpp, and agent-generated unit tests such as test/wrapper.cpp. Produces prioritized, evidence-based findings and checks test quality, timing semantics, coverage, and false-positive risks.'
argument-hint: 'Provide the diff, files, commit, or review scope'
---

# verilator_utils Code Review

## When to Use
- Review changes under `src/`, `test/`, `rtl/`, or their `xmake.lua` files.
- Review a pull request, commit, working-tree diff, or selected framework file.
- Audit coroutine scheduling, Verilator data wrappers, DUT context ownership, tracing, or module exports.
- Evaluate whether tests are semantically meaningful rather than merely compiling or increasing line coverage.
- Distinguish hand-written RTL integration tests from broad agent-generated unit tests and apply the appropriate standard to each.

## Review Goal
Find concrete defects introduced or exposed by the reviewed change. Prioritize correctness, lifetime safety, simulation semantics, public API contracts, portability, and regression protection. Do not turn style preferences, speculative redesigns, or pre-existing unrelated problems into findings.

## Required Review Process
1. Establish the exact review boundary from the requested files or diff. Read enough adjacent declarations, implementations, tests, and build rules to understand changed behavior.
2. Identify the affected contract: public module API, ownership/lifetime, scheduler phase, simulation time, bit range/format, tracing, build registration, or test expectation.
3. Trace each changed path through callers and consumers. For exported symbols, search all uses before claiming compatibility or lifetime problems.
4. Classify changed tests as hand-written RTL integration tests or agent-generated unit tests using their target, DUT dependency, structure, and intent; do not infer quality from authorship alone.
5. Validate suspicious behavior with the narrowest useful test or build configuration when practical. Use sanitizers or standard-library hardening when the suspected defect is lifetime, undefined behavior, or iterator invalidation.
6. Report only actionable findings with a reproducible trigger, a concrete impact, and precise changed-code evidence. If no such finding exists, say so and summarize residual validation gaps.

## Finding Standard
A review finding must satisfy all of the following:
- It identifies behavior that is incorrect, unsafe, incompatible, flaky, or insufficiently tested for a changed contract.
- It explains the input, state, scheduling order, platform, or configuration that triggers the problem.
- It states the observable impact rather than only naming a rule.
- It points to the smallest relevant changed range; use nearby unchanged code only to explain the causal chain.
- It proposes the direction of a fix without requiring an unrelated redesign.

Use these priorities:
- `P0`: unconditional catastrophic impact, such as pervasive corruption or unusable releases.
- `P1`: high-impact correctness, memory safety, deadlock, systematic simulation error, or common build breakage.
- `P2`: real defect on a plausible edge case, configuration, data width, or scheduling path.
- `P3`: limited but concrete maintainability or test-trust problem that can hide future regressions.

Do not report formatting, naming, comment language, preferred assertion spelling, or hypothetical misuse unless repository rules make it a correctness issue.

## Framework Code Review

### C++ Modules and Public API
- Check that intended APIs are reachable through the correct exported module or partition and that implementation-only details are not accidentally exported.
- Check module purview, global module fragment includes, `extern "C++"` includes, and dependency imports for ODR or compiler-portability risks.
- Treat API compatibility deliberately: deleted copy/move operations, changed constraints, return/reference categories, exception behavior, and ownership transfer can break existing tests or downstream users.
- Verify concepts against supported Verilator scalar and wide types, including cv/ref behavior when the contract is intentionally strict.

### Preconditions and Error Behavior
- Framework runtime validation must not depend on doctest assertions such as `REQUIRE_*`; those can abort the test process and couple production behavior to the test framework.
- Prefer explicit language-level contracts such as exceptions, constraints, or documented undefined/preconditioned behavior according to the existing API direction.
- Check arithmetic before validation for underflow, overflow, invalid shifts, zero widths, reversed ranges, and out-of-bounds word access.
- Verify `noexcept` functions cannot reach throwing validation, allocation, formatting, callbacks, or coroutine exception paths.

### Ownership and Lifetime
- Follow ownership of `VerilatedContext`, DUTs, tracers, schedulers, tasks, coroutine handles, callbacks, and slice objects.
- A `bit_slice` or `vector_slice` stores a reference; ensure the backing Verilator value outlives the slice and every callback or coroutine that copies it.
- Check destruction order: queued coroutine handles must not outlive the scheduler, DUT, context, captured state, or task owner.
- Check move, detach, join, destroy, and exception paths for double destruction, leaks, null-handle access, suspended children, and lost exceptions.
- For coroutine lambdas, apply the `cpp-coroutine-lambda-safety` skill: state needed after suspension belongs in explicit parameters, not a temporary closure's captures.

### Scheduler and Coroutine Semantics
- Model the actual scheduler phase order before reviewing an expectation: DUT evaluation, ready queue, event queue, timed waits, verification/stimulus stages, finish, and cooperative cancellation.
- Check immediate-ready awaiters separately because `await_suspend()` may never run; `await_resume()` must not assume state initialized only by suspension.
- Check parent/child transitions, root versus async ownership, scheduler propagation, final suspension, exception rethrowing, and early finish.
- Any callback or coroutine resume may mutate scheduler queues reentrantly. Apply the `cpp-container-iterator-invalidation` skill to traversals that erase, append, destroy, or resume tasks.
- Check equal deadlines, empty queues, one-element queues, multiple ready tasks, tasks added during evaluation, and tasks destroyed during callbacks.
- Confirm time conversions use the configured Verilator time unit and precision without truncation, overflow, invalid alignment, or incorrect zero formatting.

### Verilator Data Wrappers
- Review scalar types `CData`, `SData`, `IData`, and `QData`, plus `VlWide<N>` independently; scalar success does not prove wide correctness.
- Check width boundaries around `1`, `8`, `16`, `31/32`, `63/64`, whole words, partial top words, and cross-word slices.
- Check shift operands and mask types before shifts by the type width. Width 64 and word-aligned ranges usually need explicit handling.
- For reads and writes, verify relative versus absolute indices, inclusive bounds, preservation of surrounding bits, top-word masking, source/destination width agreement, and aliasing/self-assignment.
- For formatting and comparison, verify signed interpretation, sign magnitude, fixed point, float/double bit casts, NaN partial ordering, padding, and exact prefixes.

### DUT Context, Tracing, and Build Integration
- Verify context, DUT, scheduler, and tracer construction/destruction order and that `final()`, trace dump, coverage write, and finish paths occur exactly when intended.
- Treat cross-DSO Verilator RTTI and tracer creation as platform-sensitive; ensure runtime objects are created and destroyed in ABI-compatible linkage units.
- Check trace feature options, generated model dependencies, compiler runtime selection, PIC/static/shared boundaries, and required compression libraries.
- Verify xmake file filters and test names include the intended files exactly once and map to the doctest suite actually present.
- Distinguish failures caused by stale C++ module BMI/object files from source defects; rebuild narrowly with `-r` or reconfigure before proposing unrelated code changes.

## Hand-Written RTL Integration Tests
Examples include `test/rtl_edge_detector.cpp` and tests built from `rtl/*.sv` plus generated Verilator models.

Review these as executable timing specifications:
- Derive expected values from RTL latency, clock polarity, reset semantics, sampling edge, nonblocking assignment behavior, and scheduler phase order.
- Check asynchronous stimulus around edges, synchronous stimulus on both sides of the sampling edge, first active edge after reset, stable/no-input cycles, and pipeline drain.
- Ensure every spawned verification task is owned and joined; the test must not finish while checks remain suspended.
- Ensure `eval_finish()` occurs only after all expected outputs are observed and optional waveform-drain cycles complete.
- Check that local port wrappers, DUT/context objects, spawn pools, and referenced state outlive all coroutine tasks.
- Prefer explicit timing intent over incidental delays. A passing waveform is insufficient if assertions sample the wrong cycle or phase.
- Verify reset helpers and clock generators agree with the RTL's active level and edge sensitivity.
- Assess determinism under Verilator's configured `--x-assign`, `--x-initial`, tracing, and coverage options; do not encode accidental random initialization as an expectation.
- Require assertions for the semantic outputs and completion conditions, not merely absence of timeout or successful simulation exit.

When reviewing changes to an RTL integration test, inspect the corresponding `rtl/*.sv`, generated-model target, and helper semantics before declaring an expected value wrong.

## Agent-Generated Unit Tests
Examples include broad component tests such as `test/wrapper.cpp`, `test/scheduler.cpp`, and similar tests authored or expanded by an agent.

Review these for trustworthiness and contract coverage:
- Independently recompute expected constants, masks, sign interpretations, formatted strings, word ordering, and simulated timestamps. Generated expected values are not evidence of correctness by themselves.
- Detect copy/paste gaps: duplicated type cases, omitted `IData`/`QData`/`VlWide`, repeated assertions that exercise the same path, mismatched test names, and variables that are constructed but never meaningfully checked.
- Ensure each test can fail for the intended bug. Assertions must observe externally relevant state after the operation, including preservation of unaffected bits and task completion/exception state.
- Prefer direct public API behavior over duplicating implementation algorithms in the test; mirrored algorithms can reproduce the same defect and pass.
- Require boundary matrices where the implementation branches: scalar versus wide, single versus multiword, aligned versus cross-word, zero/min/max width, signed versus unsigned, immediate-ready versus suspended, and one versus multiple queued tasks.
- Check invalid-input tests against the API's real error contract. Do not accept tests that rely on doctest fatal assertions inside framework code as runtime validation.
- For coroutine unit tests, ensure coroutine lambdas have empty captures and explicit parameters, task owners outlive execution, exceptions are rethrown or asserted, and the scheduler is driven enough to prove completion.
- Reject assertions that depend on unspecified ordering when the contract guarantees only set membership; conversely, require exact order when scheduler ordering is part of the contract.
- Watch for suite/filter mismatches: `test/<basename>.cpp` is run with `-ts=verilator_utils/<basename>`, so the suite name must allow the intended cases to execute.
- Check test isolation: no leaked global Verilator state, stale waveform/coverage assumptions, order dependence, wall-clock sleeps, or random values without deterministic seeds and invariants.

Large test volume is not a substitute for mutation-sensitive checks. Sample representative tests by asking: what one-line implementation defect would this test catch, and would the assertion actually fail?

## Testing the Tests
When a new or suspicious test appears vacuous, use one of these techniques if the scope permits:
1. Temporarily perturb the relevant implementation branch and confirm the narrow test fails, then restore the implementation.
2. Compare the expected result with an independent hand calculation, RTL timing table, or simpler reference representation.
3. Run only the named target with verbose doctest output and confirm the intended case count/suite executes.
4. Exercise an adjacent boundary that selects the other implementation branch.

Do not leave deliberate mutations in the final change.

## Validation Commands
Start with the narrowest affected test:

```bash
xmake test -v unit_test/<basename>
xmake test -v unit_test_rtl_edge_detector/rtl
```

If C++ module artifacts may be stale:

```bash
xmake test -r -v unit_test/<basename>
```

For iterator/lifetime-sensitive scheduler review under libstdc++ hardening:

```bash
xmake f --toolchain=clang -m debug --runtimes=stdc++_shared --use_sanitizer=no --use_std_harden=yes -c
xmake test -v unit_test/scheduler
```

For memory and undefined-behavior concerns, use the repository sanitizer option when compatible with the selected runtime and generated Verilator target:

```bash
xmake f -m debug --use_sanitizer=yes -c
xmake test -v unit_test/<basename>
```

Finish source edits with:

```bash
git diff --check
```

Do not claim broad portability or full regression coverage from one compiler/runtime configuration. State exactly which target and configuration were validated.

## Review Output
List findings first, ordered by priority and then source location. Each finding should contain:
- A concise title prefixed with `[P0]` through `[P3]`.
- The smallest useful file/line range.
- A short explanation of the trigger, causal chain, and user-visible or test-visible impact.
- A focused fix direction when it is not obvious.

After findings, optionally list:
- Validation commands actually run and their results.
- Residual risks or untested configurations.

If there are no actionable findings, explicitly state that no concrete defects were found; do not invent low-value comments to fill the review.

## Final Checklist
- Review boundary and changed contract are explicit.
- Public module and downstream usages were checked where relevant.
- Coroutine, callback, and referenced-object lifetimes are valid across suspension.
- Scheduler expectations match actual phase and mutation order.
- Scalar, wide, boundary-width, and cross-word wrapper paths are considered.
- Runtime validation is independent of doctest internals.
- Hand-written RTL tests prove sampling, reset, latency, joining, and finish semantics.
- Agent-generated tests have independently verified expectations and no obvious copy/paste coverage holes.
- xmake filters, suite names, and requested test targets execute the intended cases.
- Findings are concrete, prioritized, changed-code-specific, and free of style-only noise.