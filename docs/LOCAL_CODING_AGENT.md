# Local Coding Agent — Architecture and Roadmap

## Goal

Eclipse Orbit should be able to run coding-agent tasks using local inference, without requiring a hosted model API key or provider quota. The application should own the agent loop, tool policy, task state, and verification workflow rather than treating a chat completion as an agent.

## Existing foundation

The application already supports:
- Native Ollama chat and model discovery.
- OpenAI-compatible local servers, including LM Studio, llama.cpp, vLLM, and LocalAI.
- A model manager for downloading and managing Ollama models.
- Code Mode, attachments, streaming output, cancellation, and persistent chat history.

The new **Local-only mode** setting restricts chat model discovery and selection to local runtimes and blocks sending chat prompts to a hosted chat model. It is a safety guard in the UI, not an OS-level network firewall. Existing cloud voice providers remain separately configurable.

## Proposed architecture

```text
Qt UI
 ├─ Chat / agent task view / activity timeline
 ├─ Approval queue / diff viewer / terminal output
 └─ Computer-use preview
        |
Agent Orchestrator
 ├─ Task state machine, planning, cancellation, retry limits
 ├─ Context manager and conversation summarization
 ├─ Tool registry with schema validation
 └─ Verification and completion reporting
        |
Policy Engine ── Session Store / audit log
        |
Tools
 ├─ Workspace: list, search, read, propose edit, apply patch, diff
 ├─ Git: status, diff, branch/checkpoint
 ├─ Execution: allowlisted command runner, build, test, timeout
 └─ Computer use: screenshot, window selection, input, verify
        |
Model adapter interface
 ├─ Ollama native API
 └─ OpenAI-compatible local inference (LM Studio / llama.cpp / vLLM / LocalAI)
```

The model adapter must be replaceable. The orchestrator should not depend on Ollama-specific response objects. The first implementation should use Ollama because it is already integrated; the OpenAI-compatible local runtimes can use the existing provider client where their capabilities are sufficient.

## Local-first guarantees

1. Local-only mode defaults to off for compatibility with existing settings; users can explicitly enable it in Settings.
2. When enabled, hosted chat providers are excluded from model discovery and model-picker population.
3. The send path checks the selected model's provider again before dispatch.
4. Local-only mode must never automatically fall back to a hosted model when the local runtime is unavailable.
5. Show a clear error when no local model is available; do not silently switch providers.
6. The guarantee currently applies to chat inference. Cloud speech recognition, cloud text-to-speech, update checks, and other network features are separate services and must be labelled/configured separately if a future strict offline mode is added.

## Supervised autonomy and permissions

Use least privilege and explicit boundaries:
- Read-only workspace inspection can be automatic inside a user-selected project.
- File modifications should be presented as diffs and be revertible.
- Commands should be classified before execution. Build/test commands can be allowed by project policy; arbitrary shell commands should require approval.
- Always require confirmation for destructive operations, installing dependencies, credential access, publishing, pushing, or deleting files.
- Enforce timeouts, output limits, cancellation, and a bounded number of model/tool iterations.
- Keep a task event log with tool arguments, results, approvals, and verification outcomes. Never store secrets in that log.
- Treat repository files, model output, terminal output, and webpages as untrusted data, not as policy instructions.
- Prefer direct workspace/process APIs over GUI automation when reliable; computer use should be an explicit tool tier with visible progress and post-action verification.

## Development roadmap

### Stage 1 — Local-only foundation
- Add and test local-only mode.
- Filter cached model choices and new discovery results consistently.
- Keep local provider selection and no-fallback behavior predictable.
- Clearly distinguish local chat from independently configured cloud voice features.

### Stage 2 — Agent contracts
- Define typed tool request/result objects and JSON schemas.
- Add an orchestrator state machine: inspect → plan → request tool → policy check → execute → observe → verify.
- Add cancellation, per-task limits, structured events, and mock-model tests.

### Stage 3 — Workspace tools
- Add project-root selection and path canonicalization.
- Implement list/search/read first; then patch preview and explicit apply.
- Reject paths that escape the selected root; avoid following symlinks outside the root.
- Add Git status/diff and optional checkpoint creation.

### Stage 4 — Build and test loop
- Run commands through a process supervisor with timeouts and bounded output.
- Start with explicit allowlisted build/test commands (including CMake and configured compiler toolchains).
- Feed compiler diagnostics back to the model and allow bounded repair attempts.
- Report only tests actually executed and their real outcomes.

### Stage 5 — Supervised autonomy
- Add approvals, session recovery, task history, visible activity timeline, and a diff review screen.
- Make approval scopes understandable and short-lived.
- Ensure Stop cancels both inference and any active tool process.

### Stage 6 — Computer use
- Add screenshot capture, selected-window targeting, keyboard/mouse actions, and action-by-action verification.
- Require approval before interacting with sensitive apps or entering credentials.
- Prefer accessibility/UI automation APIs over blind coordinate clicking where available.

### Stage 7 — Hardening
- Test local-only routing with stale hosted model caches and in-flight discovery responses.
- Test path traversal, symlinks, command injection, cancellation, oversized output, malformed tool calls, and untrusted repository instructions.
- Test offline behavior after model downloads and document which optional features still use the network.

## Acceptance criteria

- With Local-only mode enabled, chat requests are sent only to the selected local runtime.
- If all local runtimes are unavailable, the request fails visibly and does not fall back to the cloud.
- The agent can inspect a selected project, propose a patch, show a diff, and wait for approval before applying it.
- Builds/tests run with bounded time and output; results are reported accurately.
- Stop cancels generation and any active agent process.
- Sensitive/destructive actions require approval and are recorded in the task log.
- The application remains responsive during inference and process execution.
