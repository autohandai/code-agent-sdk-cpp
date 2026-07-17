# Configuration

The C++ SDK keeps configuration close to the Autohand CLI contract. Most fields become CLI flags when the subprocess starts.

## Basic Configuration

```cpp
auto config = autohand::Config::from_environment()
    .with_cwd(".")
    .with_model("moa")
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
- `bare`: minimal explicit runtime.
- `idle_logout = false`: keep authenticated long-running sessions alive.
- `fork_session`: fork an existing session before RPC startup.
- `display_language`: CLI display locale.
- `system_prompt_file`, `append_system_prompt_file`: prompt files.
- `mcp_config`, `agents`, `plugin_dir`: explicit integration paths.
- session persistence, resume/continue, token/compaction, AGENTS.md, and
  skill-source/install options map to the current CLI flags.

Runtime feature settings are applied after startup with
`sdk.apply_flag_settings(R"({"features":{"slashGoal":true}})")`.
Set `feature_settings_json` on `Config` to apply the same payload automatically
as part of SDK startup.

## Autohand AI Provider

```cpp
auto config = autohand::Config::from_environment();
config.provider = "autohandai";
config.model = "moa";
config.api_key = std::getenv("AUTOHAND_AI_API_KEY");
config.autohand_ai_plan = "cloud";
```

The SDK maps the API key, optional base URL, and plan to `AUTOHAND_AI_*`
subprocess environment variables. `Config::from_environment()` reads those
variables directly, and explicit entries in `environment` take precedence.

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
