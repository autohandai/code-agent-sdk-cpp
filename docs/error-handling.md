# Error Handling

SDK errors fall into four categories.

## Transport Errors

The CLI subprocess could not start, disconnected, or returned invalid JSON.

Common causes:

- `AUTOHAND_CLI_PATH` points to a missing binary.
- The CLI is not authenticated.
- The provider config is invalid.

## Request Timeouts

Requests time out according to `Config::timeout`.

```cpp
auto config = autohand::Config::from_environment();
config.timeout = std::chrono::milliseconds(120000);
```

## JSON-RPC Errors

The CLI rejected a request. Examples:

- calling a control method before `start()`;
- sending an expired permission request id;
- setting an unsupported model.

## Agent Events

Agent loop failures may arrive as `error` events in the stream. Handle them in your event callback and show enough context for users to recover.

## Recovery Patterns

- Stop and restart the SDK after transport failures.
- Use `interrupt()` or `Run::abort()` for user cancellation.
- Keep final summaries honest when checks fail.
- Keep raw event JSON available for debugging advanced CLI behavior.
