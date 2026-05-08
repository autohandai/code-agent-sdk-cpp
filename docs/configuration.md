# Configuration

The C++ SDK keeps configuration close to the Autohand CLI contract. Most fields become CLI flags when the subprocess starts.

## Basic Configuration

```cpp
auto config = autohand::Config::from_environment()
    .with_cwd(".")
    .with_model("fantail2")
    .with_skill("cpp")
    .with_instructions("Prefer safe, idiomatic C++.");
```

`Config::from_environment()` reads `AUTOHAND_CLI_PATH` when present.

## Provider Credentials

Provider credentials are owned by the Autohand CLI, not the SDK. Configure them in `~/.autohand/config.json` or through environment variables supported by the CLI.

```json
{
  "provider": "openrouter",
  "openrouter": {
    "apiKey": "sk-or-...",
    "model": "openrouter/auto"
  }
}
```

## Runtime Options

Common options:

- `model`: model override.
- `temperature`: sampling temperature.
- `max_iterations`: loop limit.
- `max_runtime_minutes`: wall-clock limit.
- `max_cost`: cost budget.
- `context_compact`: context compaction.
- `additional_directories`: extra workspace roots.
- `skills`: skills available to the agent.
- `environment`: environment variables for the CLI subprocess.

## System Prompts

Use `with_instructions()` or `append_system_prompt` for normal integrations. Replacing `system_prompt` means your host owns the full agent contract.

## Permissions

Use `unrestricted` only for trusted automation. For most applications, keep the default interactive behavior and respond to `permission_request` events.

```cpp
sdk.set_permission_mode("interactive");
```

## Plan Mode

Plan mode is a runtime control:

```cpp
sdk.set_plan_mode(true);
```

See [Plan Mode](./plan-mode.md).
