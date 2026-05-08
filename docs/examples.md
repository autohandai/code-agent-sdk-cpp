# Examples

The C++ examples mirror the TypeScript SDK examples and are intended to teach one workflow at a time.

Build one with:

```bash
cmake -S . -B build -DAUTOHAND_BUILD_EXAMPLES=ON
cmake --build build --target example_01_hello_agent
./build/example_01_hello_agent
```

Examples:

- `01-hello-agent.cpp`: first prompt.
- `02-streaming-query.cpp`: stream message events.
- `03-code-reviewer.cpp`: inspect repository files.
- `04-bash-command.cpp`: command-oriented prompt with permissions.
- `05-file-editor.cpp`: file-editing workflow.
- `06-prompt-skills.cpp`: skill-aware prompting.
- `07-direct-skills.cpp`: preconfigured skills.
- `08-memory-management.cpp`: memory save and recall pattern.
- `10-multi-tool-reasoning.cpp`: inspect, test, and summarize.
- `13-permissions.cpp`: explicit permission mode.
- `20-sdlc-discovery-plan.cpp`: plan-only discovery.
- `21-sdlc-gated-implementation.cpp`: plan then execute.
- `22-sdlc-release-readiness.cpp`: release gate.
- `23-system-prompts.cpp`: appended system instructions.
- `24-high-level-agent.cpp`: `Agent` and `Run`.
- `25-structured-json.cpp`: structured JSON output.

Live examples require an authenticated Autohand CLI.
