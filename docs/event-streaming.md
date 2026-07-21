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

## Typed CLI Hook Notifications

The 16 `autohand.hook.*` notifications decode into alternatives of
`SdkEvent::payload`:

| RPC method | `event.type` | Payload alternative |
| --- | --- | --- |
| `autohand.hook.preTool` | `hook_pre_tool` | `PreToolHookEvent` |
| `autohand.hook.postTool` | `hook_post_tool` | `PostToolHookEvent` |
| `autohand.hook.fileModified` | `file_modified` | `FileModifiedHookEvent` |
| `autohand.hook.prePrompt` | `hook_pre_prompt` | `PrePromptHookEvent` |
| `autohand.hook.postResponse` | `hook_post_response` | `PostResponseHookEvent` |
| `autohand.hook.sessionError` | `hook_session_error` | `SessionErrorHookEvent` |
| `autohand.hook.stop` | `hook_stop` | `StopHookEvent` |
| `autohand.hook.sessionStart` | `hook_session_start` | `SessionStartHookEvent` |
| `autohand.hook.sessionEnd` | `hook_session_end` | `SessionEndHookEvent` |
| `autohand.hook.subagentStop` | `hook_subagent_stop` | `SubagentStopHookEvent` |
| `autohand.hook.permissionRequest` | `hook_permission_request` | `PermissionRequestHookEvent` |
| `autohand.hook.notification` | `hook_notification` | `NotificationHookEvent` |
| `autohand.hook.contextCompacted` | `hook_context_compacted` | `ContextCompactedHookEvent` |
| `autohand.hook.contextOverflow` | `hook_context_overflow` | `ContextOverflowHookEvent` |
| `autohand.hook.contextWarning` | `hook_context_warning` | `ContextWarningHookEvent` |
| `autohand.hook.contextCritical` | `hook_context_critical` | `ContextCriticalHookEvent` |

Use `std::get_if` to select a typed payload:

```cpp
if (const auto* warning =
        std::get_if<autohand::ContextWarningHookEvent>(&event.payload)) {
  std::cout << warning->remaining_tokens << " tokens remain\n";
}
```

Unknown notification methods and recognized hooks whose payload fails
validation remain observable. `event.method` retains the RPC method,
`event.raw_json` retains the exact top-level params value, and `event.payload`
is `std::monostate`.

Context counts (`cropped_count`, `tokens_before`, `tokens_after`, and
`remaining_tokens`) must be non-negative integers representable by
`long long`. Fractions, negative values, or values outside that range use the
raw fallback. `usage_percent` must be finite and non-negative.

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
