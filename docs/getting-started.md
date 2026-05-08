# Getting Started

The Autohand Code Agent SDK for C++ spawns the Autohand CLI in JSON-RPC mode and exposes C++20 APIs for prompts, streaming events, permissions, and structured output.

## Prerequisites

1. Install and authenticate the Autohand CLI.
2. Configure a provider in `~/.autohand/config.json`.
3. Install CMake 3.22 or later and a C++20 compiler.

Set a custom CLI path when developing locally:

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

## Your First Agent

```cpp
#include <autohand/sdk.hpp>
#include <iostream>

int main() {
  autohand::Agent agent(autohand::Config::from_environment().with_cwd("."));
  auto result = agent.run("Summarize this repository.");
  std::cout << result.text << "\n";
  agent.close();
}
```

## Streaming

```cpp
autohand::AutohandSdk sdk(autohand::Config::from_environment().with_cwd("."));
sdk.start();

sdk.stream_prompt("Explain the SDK in one paragraph.", [](const autohand::SdkEvent& event) {
  if (event.type == "message_update") {
    std::cout << event.text_delta();
  }
});

sdk.stop();
```

## Next Steps

- Read [Configuration](./configuration.md).
- Try [Event Streaming](./event-streaming.md).
- Learn [Permissions](./permissions.md).
- Use [SDLC Workflows](./sdlc-workflows.md) for production changes.
