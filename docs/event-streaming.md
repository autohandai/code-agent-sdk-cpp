# Event Streaming

`stream_prompt()` starts a prompt and invokes your callback as events arrive.

## Basic Pattern

```cpp
sdk.stream_prompt("Explain closures in one sentence.", [](const autohand::SdkEvent& event) {
  if (event.type == "message_update") {
    std::cout << event.text_delta();
  }
});
```

Calls to `stream_prompt()` on the same SDK instance are serialized. Each
callback therefore receives the notifications for its own prompt without a
concurrent stream clearing or draining them.

## Event Types

- `message_update`: token or text delta.
- `message_end`: final message content.
- `tool_start`: a tool started.
- `tool_update`: streaming tool output.
- `tool_end`: a tool completed.
- `permission_request`: host approval is required.
- `error`: agent or transport error.
- `turn_end`: includes raw `tokensUsed`, `tokensUsageStatus`, `durationMs`, and
  `contextPercent` fields when the CLI reports them.
- `autoresearch`: lifecycle and ledger-operation notifications. Inspect
  `raw_json` for `phase`, `operation`, `attemptId`, `success`, and retention data.

## Handling Permissions While Streaming

```cpp
sdk.stream_prompt("Run the relevant tests.", [&](const autohand::SdkEvent& event) {
  if (event.type == "permission_request") {
    sdk.permission_response(event.request_id(), "allow_once");
  }
});
```

Production hosts should route permission requests to a human, policy engine, or trusted automation boundary.

## Collecting Final Text

Use `Agent` and `Run` when you want streaming and a final result:

```cpp
auto run = agent.send("Summarize this repository.");
run.stream([](const autohand::SdkEvent& event) {
  std::cout << event.type << "\n";
});
auto result = run.wait();
std::cout << result.text << "\n";
```

If a stream callback throws, the run becomes terminally failed. Later calls to
`wait()` rethrow that same exception and do not send the prompt again.
