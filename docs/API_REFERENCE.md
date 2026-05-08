# API Reference

## `autohand::Config`

Configuration used to start the Autohand CLI subprocess.

Common fields:

- `cwd`: working directory for the agent.
- `cli_path`: optional Autohand CLI binary path.
- `debug`: print CLI diagnostic output.
- `timeout`: JSON-RPC request timeout.
- `model`: model override passed to the CLI.
- `skills`: skills passed to the CLI.
- `append_system_prompt`: additional system instructions.
- `unrestricted`, `auto_mode`, `auto_skill`, `auto_commit`: execution mode flags.
- `context_compact`: enable or disable context compaction.
- `yolo`, `yolo_timeout_seconds`: unattended permission policy.

Helpers:

```cpp
auto config = autohand::Config::from_environment()
    .with_cwd(".")
    .with_model("fantail2")
    .with_skill("cpp")
    .with_instructions("Prefer small, typed C++ APIs.");
```

## `autohand::AutohandSdk`

Low-level JSON-RPC wrapper.

Important methods:

- `start()` / `stop()`
- `request(method, params_json)`
- `prompt(message, options)`
- `stream_prompt(message, on_event, options)`
- `interrupt()`
- `set_plan_mode(enabled)`
- `set_permission_mode(mode)`
- `set_model(model)`
- `get_state()`
- `get_messages()`
- `permission_response(request_id, decision)`

## `autohand::Agent`

High-level API for application code.

```cpp
autohand::Agent agent(autohand::Config::from_environment().with_cwd("."));
auto run = agent.send("Review the public API.");
auto result = run.wait();
agent.close();
```

Methods:

- `send(prompt, options)`
- `run(prompt, options)`
- `run_json(prompt, schema_json)`
- `allow_permission(request_id)`
- `deny_permission(request_id)`
- `set_plan_mode(enabled)`
- `close()`

## `autohand::Run`

Represents a single agent run.

- `stream(on_event)`: stream events and record final text.
- `wait()`: wait until the run finishes and collect text/events.
- `json_text()`: parse final output as JSON text.
- `abort()`: interrupt the current run.

## `autohand::SdkEvent`

Typed event envelope with raw JSON access.

Helpers:

- `text_delta()`
- `message_content()`
- `tool_name()`
- `request_id()`
- `description()`

Event types include:

- `agent_start`
- `turn_start`
- `message_update`
- `message_end`
- `tool_start`
- `tool_update`
- `tool_end`
- `permission_request`
- `error`

## Structured JSON

```cpp
auto json = agent.run_json(
    "Assess release readiness.",
    R"({"summary":"string","risks":[]})");
```
