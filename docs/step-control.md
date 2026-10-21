# Step control

Stop after the CLI persists completed tool results, inspect the steps, then
continue the same agent. The CLI must support `autohand.stepEnd`,
`autohand.stepDecision`, and host-mode `stopWhen`.

```cpp
autohand::Config config = autohand::Config::from_environment();
config.provider = "autohandai";
config.model = "fantail";
autohand::Agent agent(config);

autohand::PromptOptions options;
options.stop_when = {autohand::is_step_count(1)};
const auto result = agent.run("Read README.md using read_file", options);
std::cout << result.status << ": " << result.steps.size() << " steps\n";
const auto continued = agent.run("Summarize the saved result.");
std::cout << continued.text << '\n';
```

`is_step_count(n)` requires a positive count. `has_tool_call(name)` trims the
name and checks the latest completed step. Multiple conditions use OR. Each
condition sees a read-only snapshot of the current prompt's completed steps;
later prompts start a fresh step history while retaining the CLI conversation.

Custom conditions return either `bool` or `std::shared_future<bool>`:

```cpp
options.stop_when = {
    [](const autohand::StopConditionContext& context,
       autohand::CancellationToken cancellation) {
      if (cancellation.stop_requested()) return false;
      return !context.steps.back().tool_results.front().success;
    }
};
```

The SDK invokes each condition on a host worker. A condition can return a shared
future to await external work. Workers retain their condition and immutable
context until that work settles. Conditions should observe the cancellation
token and stop their own work when requested. A blocked condition does not block
CLI events or cancellation, and its late result cannot send another decision.

Only `{ "mode": "host" }` is serialized as `stopWhen`; functions remain in the
host process. Condition exceptions cause a stop decision, wait for terminal
cleanup, and propagate to the caller. Malformed steps and rejected decisions
also fail the run. The next prompt starts after cleanup; failed cleanup stops
the CLI process.

`RunResult.steps` contains typed `AgentStep` values. Calls contain `tool`, optional
`id`, and structured `args_json`; results contain `tool`, `success`, optional
`output`, and optional `error`. Streaming callbacks receive the same step as
`std::get_if<autohand::StepEndEvent>(&event.payload)`. A CLI terminal reason of
`stop_condition` produces result status `stopped`.

`Run` remains lazy: `wait()` or `stream()` starts it. `abort()` requests
cancellation of that run; `wait()` observes its completion. Canceling an
unstarted, queued, or completed run cannot interrupt another run. A callback
exception also cleans up the active prompt and is retained by later `wait()`
calls. `AutohandSdk::interrupt()` remains an explicit session-wide interrupt.

Both `prompt()` and `stream_prompt()` wait for a terminal notification and the
prompt RPC response. Code that needs only acknowledgement can use
`request("autohand.prompt", parameters_json)` directly.

Build and run the example:

```sh
cmake -S . -B build
cmake --build build --target example_28_step_control
./build/example_28_step_control
```

To run the real CLI integration, install Python 3 for the local HTTP mock and
set `AUTOHAND_TEST_CLI_PATH` before running CTest. That test uses an isolated
configuration and local authentication/inference endpoints.
