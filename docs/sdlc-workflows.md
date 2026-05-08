# SDLC Workflows With The C++ SDK

These workflows mirror the TypeScript SDK and use the C++ SDK as an inspectable orchestration layer around the Autohand CLI.

## Discovery And Planning

Use `examples/20-sdlc-discovery-plan.cpp` for ambiguous work. It starts from plan mode and asks the agent to produce a plan without editing files.

```cpp
sdk.set_plan_mode(true);
```

Ask for:

- scope
- risks
- test strategy
- rollout steps
- explicit non-goals

## Gated Implementation

Use `examples/21-sdlc-gated-implementation.cpp` as the model:

1. Generate a plan.
2. Review it outside the agent loop.
3. Disable plan mode.
4. Execute with permission handling.

## Release Readiness

Use `examples/22-sdlc-release-readiness.cpp` to ask the agent to run or explain the release gate.

Recommended C++ gate:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DAUTOHAND_BUILD_TESTS=ON -DAUTOHAND_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Host Responsibilities

The host application should:

- keep approval gates outside the model response;
- surface permission requests clearly;
- record what commands were run;
- summarize residual risk;
- keep generated changes reviewable by humans.
