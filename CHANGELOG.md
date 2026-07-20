# Changelog

## Unreleased

### Added

- Typed community skill registry discovery and installation.
- Typed MCP server, tool, and configuration discovery.
- A deterministic three-metric startup performance gate and baseline.

### Fixed

- Made CLI startup report `chdir` and `exec` failures and verify `getState`
  readiness before returning.
- Made request writes partial-write-safe and immune to process-wide `SIGPIPE`
  termination.
- Failed pending requests immediately when CLI stdout closes.
- Serialized stream consumers so concurrent prompts cannot clear or steal each
  other's notifications.
- Made failed `Run::stream` calls terminal and rethrow the original callback
  failure from later `wait` calls without resending the prompt.
- Cleaned up stale reader threads and process descriptors before restarting
  after unexpected stdout EOF.
- Added a bounded `SIGTERM` shutdown window followed by `SIGKILL` fallback.
- Replaced regex and substring JSON-RPC parsing with a standards-aware parser
  supporting whitespace, escapes, Unicode surrogate pairs, arrays, and nested
  objects.
