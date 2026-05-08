# Permissions

The Autohand CLI asks before running shell commands, writing files, or taking sensitive actions. The SDK surfaces those requests as `permission_request` events.

## Recommended Default

Keep permission handling interactive unless your host has a clear trust boundary:

```cpp
sdk.set_permission_mode("interactive");
```

## Responding To Requests

```cpp
if (event.type == "permission_request") {
  sdk.permission_response(event.request_id(), "allow_once");
}
```

Common decisions:

- `allow_once`
- `deny_once`

## Agent Helpers

```cpp
agent.allow_permission(request_id);
agent.deny_permission(request_id);
```

## Production Guidance

- Show the tool name and description to the user.
- Deny by default when request context is missing.
- Avoid blanket approval for file writes or shell commands.
- Strip secrets from logs before attaching them to issues.
- Use plan mode for discovery before allowing writes.
