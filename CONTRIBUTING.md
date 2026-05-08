# Contributing to the Autohand Code Agent SDK for C++

Thanks for helping improve the C++ SDK. This repository is open source and sits beside the public Autohand Code CLI and the other Agent SDK language packages.

## Before You Start

- Read the Agent SDK docs: https://autohand.ai/docs/agent-sdk/
- Search existing issues before opening a new one.
- Keep public API changes small, predictable, and ergonomic for C++ hosts.
- Do not commit secrets, provider keys, private logs, or local machine paths.

## Development Setup

```bash
git clone https://github.com/autohandai/code-agent-sdk-cpp.git
cd code-agent-sdk-cpp
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DAUTOHAND_BUILD_TESTS=ON -DAUTOHAND_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Live examples require an authenticated Autohand CLI. Set `AUTOHAND_CLI_PATH` if you want to test against a local CLI build:

```bash
export AUTOHAND_CLI_PATH=/path/to/autohand
./build/example_01_hello_agent
```

## Pull Requests

Good SDK pull requests usually include:

- A focused API or behavior change.
- Tests for transport, JSON parsing, or configuration behavior.
- Updated examples when public APIs change.
- Updated docs when behavior, setup, or workflows change.

## Commit Style

Use Conventional Commits, following the same style as Autohand Code CLI:

```text
feat: add Windows transport hook
fix: allow permission responses during streaming
docs: document CMake integration
test: cover structured JSON extraction
```

## Review Principles

We optimize for humans using the API:

- Prefer boring, obvious C++ over clever abstractions.
- Keep the low-level JSON-RPC escape hatch available.
- Make permissions visible to host applications.
- Keep examples small and buildable.
- Keep docs honest about beta status and POSIX transport limitations.

## Community

By participating, you agree to follow the repository [Code of Conduct](./CODE_OF_CONDUCT.md).
