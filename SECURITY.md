# Security Policy

## Reporting Security Issues

Please do not open public GitHub issues for security vulnerabilities.

Email reports to [security@autohand.ai](mailto:security@autohand.ai). Include:

- A description of the vulnerability.
- Steps to reproduce it.
- Affected SDK version or commit.
- Whether the issue involves the Autohand CLI, provider credentials, tool permissions, subprocess handling, or local file access.

We will review the report and coordinate next steps privately.

## Sensitive Information

When filing normal bugs or feature requests:

- Remove API keys and provider tokens.
- Redact local file paths when needed.
- Do not paste private repository contents unless they are required and safe to share.
- Be careful with agent logs because they can include prompts, tool output, or file snippets.

## Scope

This policy covers the C++ SDK repository and its interaction with the Autohand CLI JSON-RPC mode. Security issues in the CLI itself may also affect SDK users; include that context in your report if relevant.
