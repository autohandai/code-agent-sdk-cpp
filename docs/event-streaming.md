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

## Event Types

- `message_update`: token or text delta.
- `message_end`: final message content.
- `tool_start`: a tool started.
- `tool_update`: streaming tool output.
- `tool_end`: a tool completed.
- `permission_request`: host approval is required.
- `error`: agent or transport error.

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
