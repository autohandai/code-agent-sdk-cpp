# Autohand Code Agent SDK for C++

Modern C++20 SDK for building applications that control Autohand code agents through the Autohand CLI JSON-RPC mode.

**Documentation:** https://autohand.ai/docs/agent-sdk/

**Beta:** this SDK is actively evolving while the Agent SDK APIs stabilize. Pin versions in production and review release notes before upgrading.

## What It Does

The C++ SDK wraps the existing Autohand CLI process and exposes a small host-friendly API:

```text
C++ app -> autohand::sdk -> Autohand CLI subprocess -> provider -> model
```

Use it when you want Autohand inside native developer tools, editors, desktop apps, automation services, or CI utilities without reimplementing the CLI agent protocol.

## Features

- C++20 API with value-oriented configuration
- CMake target: `autohand::sdk`
- CLI subprocess transport over JSON-RPC 2.0
- Typed event helpers for message deltas, tools, permissions, and errors
- Typed decoding for all 16 CLI hook notifications, with exact raw fallback for unknown or malformed payloads
- High-level `Agent` and `Run` workflow
- Low-level `AutohandSdk` control methods
- Slash-command helpers, persistent goals, and the replayable autoresearch ledger
- Structured JSON extraction helper
- Example parity with the TypeScript SDK examples
- Typed community-skill discovery/installation and MCP server/tool/configuration inspection
- Transactional CLI readiness, bounded shutdown, and deterministic sub-50 ms startup gates

## Requirements

- C++20 compiler
- CMake 3.22 or later
- POSIX runtime for the initial transport implementation
- Autohand CLI installed and authenticated
- A configured provider in `~/.autohand/config.json`, or environment variables accepted by the CLI

Set `AUTOHAND_CLI_PATH` when you want to force a local CLI binary:

```bash
export AUTOHAND_CLI_PATH=/path/to/autohand
```

## Installation

Use the repository as a CMake dependency:

```cmake
include(FetchContent)

FetchContent_Declare(
  autohand_sdk
  GIT_REPOSITORY https://github.com/autohandai/code-agent-sdk-cpp.git
  GIT_TAG main
)

FetchContent_MakeAvailable(autohand_sdk)

target_link_libraries(my_app PRIVATE autohand::sdk)
```

## Quick Start

```cpp
#include <autohand/sdk.hpp>
#include <iostream>

int main() {
  autohand::Agent agent(
      autohand::Config::from_environment()
          .with_cwd(".")
          .with_instructions("Review code with senior C++ judgement."));

  auto run = agent.send("Review this repository for release readiness.");

  run.stream([](const autohand::SdkEvent& event) {
    if (event.type == "message_update") {
      std::cout << event.text_delta();
    } else if (event.type == "permission_request") {
      std::cerr << "\npermission requested: " << event.description() << "\n";
    }
  });

  auto result = run.wait();
  std::cout << "\nRun " << result.id << " finished with " << result.status << "\n";
}
```

## Low-Level Control

Use `AutohandSdk` when your host needs direct access to the JSON-RPC control surface:

```cpp
#include <autohand/sdk.hpp>
#include <iostream>

int main() {
  autohand::AutohandSdk sdk(autohand::Config::from_environment().with_cwd("."));
  sdk.start();
  sdk.set_plan_mode(true);

  sdk.stream_prompt("Create a discovery plan for this SDK change.", [](const autohand::SdkEvent& event) {
    std::cout << event.type << "\n";
  });

  sdk.stop();
}
```

## Replayable Autoresearch

The C++ SDK matches the TypeScript v1.0.3 autoresearch RPC surface while using
value-oriented C++ parameter types:

```cpp
autohand::AutoresearchStartParams params{"Reduce test runtime without regressions"};
params.metric_name = "total_ms";
params.metric_unit = "ms";
params.direction = "lower";
params.measure_command = "ctest --test-dir build";
params.max_iterations = 12;

auto started = agent.start_autoresearch(params);
auto history = agent.get_autoresearch_history();
auto pareto = agent.get_autoresearch_pareto();
auto preview = agent.prune_autoresearch(true);
agent.stop_autoresearch();
```

See [Replayable Autoresearch](./docs/autoresearch.md) for adaptive sampling,
constraints, replay, rescoring, comparison, pinning, and retention safety.

## Skill And MCP Discovery

`Agent` and `AutohandSdk` expose `get_skills_registry`, `install_skill`,
`list_mcp_servers`, `list_mcp_tools`, and `get_mcp_server_configs`. Their
request and result structures are typed, including `SkillInstallScope` and
`McpTransport` enums that match the current CLI wire contract.

## Session And Autonomous Control

- `reset()` clears the conversation and returns the new session ID.
- `create_browser_handoff()` creates a typed one-time browser handoff.
- `attach_browser_handoff()` consumes a token and attaches its session.
- `attach_latest_browser_handoff()` attaches the newest unexpired handoff.
- `start_automode()` starts a typed autonomous run and returns on acceptance.
- `get_automode_status()` reports runtime flags and typed persisted state.
- `pause_automode()` pauses the active autonomous run.
- `resume_automode()` resumes a paused autonomous run.
- `cancel_automode()` cancels a run with an optional reason.
- `get_automode_log()` returns typed autonomous iteration history.

## Startup Performance

The deterministic fake-CLI gate measures `publicImportMs`, `sdkStartReturnMs`,
and `fixtureSpawnToFirstRpcMs` with five warmups and 50 samples. Every
wrapper-controlled p95 must remain below 50 ms. See
[Startup Performance](./docs/startup-performance.md) for exact boundaries,
current results, and the separate environment-dependent live CLI/provider
readiness concern.

## Examples

The `examples/` directory mirrors the TypeScript SDK example inventory:

- `01-hello-agent.cpp`
- `02-streaming-query.cpp`
- `03-code-reviewer.cpp`
- `04-bash-command.cpp`
- `05-file-editor.cpp`
- `06-prompt-skills.cpp`
- `07-direct-skills.cpp`
- `08-memory-management.cpp`
- `10-multi-tool-reasoning.cpp`
- `13-permissions.cpp`
- `20-sdlc-discovery-plan.cpp`
- `21-sdlc-gated-implementation.cpp`
- `22-sdlc-release-readiness.cpp`
- `23-system-prompts.cpp`
- `24-high-level-agent.cpp`
- `25-structured-json.cpp`
- `27-autoresearch-ledger.cpp`
- `basic-agent.cpp`
- `basic-usage.cpp`
- `loop-strategies.cpp`
- `permission-handling.cpp`
- `sdk-control-features.cpp`
- `streaming.cpp`

Build and run an example:

```bash
cmake -S . -B build -DAUTOHAND_BUILD_EXAMPLES=ON
cmake --build build --target example_01_hello_agent
./build/example_01_hello_agent
```

Live examples require an authenticated Autohand CLI and may ask for tool permissions depending on your CLI configuration.

## Documentation

- [Getting Started](./docs/getting-started.md)
- [API Reference](./docs/API_REFERENCE.md)
- [Configuration](./docs/configuration.md)
- [Event Streaming](./docs/event-streaming.md)
- [Permissions](./docs/permissions.md)
- [Plan Mode](./docs/plan-mode.md)
- [SDLC Workflows](./docs/sdlc-workflows.md)
- [Error Handling](./docs/error-handling.md)
- [Examples](./docs/examples.md)
- [Replayable Autoresearch](./docs/autoresearch.md)
- [Startup Performance](./docs/startup-performance.md)
- [Contributing](./CONTRIBUTING.md)
- [Security](./SECURITY.md)

## Development

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DAUTOHAND_BUILD_TESTS=ON -DAUTOHAND_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The transport tests use a deterministic fake CLI, so the unit suite does not require model credentials.

## Other SDKs

- [TypeScript](https://github.com/autohandai/code-agent-sdk-typescript)
- [Python](https://github.com/autohandai/code-agent-sdk-python)
- [Go](https://github.com/autohandai/code-agent-sdk-go)
- [Java](https://github.com/autohandai/code-agent-sdk-java)
- [Swift](https://github.com/autohandai/code-agent-sdk-swift)
- [Rust](https://github.com/autohandai/code-agent-sdk-rust)
- [C#](https://github.com/autohandai/code-agent-sdk-csharp)

## Support

- SDK docs: https://autohand.ai/docs/agent-sdk/
- Issues: https://github.com/autohandai/code-agent-sdk-cpp/issues
- Security reports: security@autohand.ai
