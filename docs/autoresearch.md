# Replayable Autoresearch Ledger

The C++ SDK exposes the complete Autohand autoresearch JSON-RPC surface shipped
with the TypeScript v1.0.3 contract. It preserves a dependency-free C++20
runtime: common lifecycle values are typed, results remain raw JSON strings,
and advanced arrays or objects are passed as explicit JSON fragments.

## Capability check

```cpp
if (agent.supports_command("/autoresearch")) {
  auto run = agent.autoresearch("Improve benchmark accuracy");
  run.wait();
}
```

## Start and inspect

```cpp
autohand::AutoresearchStartParams params{"Reduce test runtime without regressions"};
params.metric_name = "total_ms";
params.metric_unit = "ms";
params.direction = "lower";
params.measure_command = "ctest --test-dir build";
params.checks_command = "cmake --build build";
params.max_iterations = 12;
params.timeout_ms = 60000;
params.files_in_scope = {"include", "src", "tests"};
params.subagents = autohand::AutoresearchSubagentOptions{true, true, false};
params.secondary_objectives_json =
    R"([{"name":"peak_memory_mb","unit":"mb","direction":"lower"}])";
params.constraints_json =
    R"([{"metricName":"failures","operator":"<=","threshold":0}])";
params.sampling_json =
    R"({"minSamples":3,"maxSamples":7,"confidenceThreshold":0.9})";
params.retention_json =
    R"({"maxArtifactBytes":100000000,"maxArtifactAgeDays":14})";

auto started = agent.start_autoresearch(params);
auto status = agent.get_autoresearch_status();
auto history = agent.get_autoresearch_history();
```

`stop_autoresearch()` pauses without deleting the persisted `.auto/` state.

## Replay and decisions

```cpp
auto original = agent.replay_autoresearch("attempt-1", "original");
auto current = agent.replay_autoresearch("attempt-1", "current");
auto rescored = agent.rescore_autoresearch("attempt-1");
auto all_rescored = agent.rescore_all_autoresearch();
auto comparison = agent.compare_autoresearch("attempt-1", "attempt-2");
auto pareto = agent.get_autoresearch_pareto();
```

Replay evaluates in an isolated worktree. Rescoring appends a new decision from
stored measurements and the current policy without rewriting evaluations.

## Pin and prune safely

```cpp
agent.pin_autoresearch("attempt-1", true);
auto preview = agent.prune_autoresearch(true);

// Apply only after inspecting the preview candidates.
auto applied = agent.prune_autoresearch(false, true);
```

Pinned and materialized candidates remain protected. Always preview retention
before applying it.
