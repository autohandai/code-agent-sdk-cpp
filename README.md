# Code Agent SDK for C++

Modern C++20 SDK for controlling Autohand code agents through the CLI JSON-RPC mode.

**Beta:** this SDK is actively evolving while the Agent SDK APIs stabilize. Pin versions in production and review release notes before upgrading.

## Quick Start

```cpp
#include <autohand/sdk.hpp>
#include <iostream>

int main() {
  autohand::Agent agent(autohand::Config::from_environment()
    .with_instructions("Review code with senior C++ judgement."));

  auto run = agent.send("Summarize this repository");
  run.stream([](const autohand::SdkEvent& event) {
    if (event.type == "message_update") {
      std::cout << event.text_delta();
    }
  });

  auto result = run.wait();
  std::cout << result.text << "\n";
}
```

## Development

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DAUTOHAND_BUILD_TESTS=ON -DAUTOHAND_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The `examples/` directory mirrors the TypeScript SDK examples, covering streaming, permissions, structured JSON, plan mode, high-level agents, and SDK control methods.

Live examples require an authenticated Autohand CLI. Set `AUTOHAND_CLI_PATH` to force a local development binary.
