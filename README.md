# Eclipse Infer

A C++20 / Qt 6 desktop AI client for local models and hosted model APIs.

## Current capabilities

- Native Qt desktop interface with theme selection, chat history, Code Mode, temperature control, and model-thinking display where supported
- System-tray integration on Windows
- Streaming chat responses
- Provider selector and configurable API endpoint
- Local Ollama model discovery and chat
- OpenAI-compatible API support for OpenAI, OpenRouter, Groq, Together AI, DeepSeek, Mistral, LM Studio, llama.cpp server, vLLM, LocalAI, and compatible custom endpoints
- Native streaming adapters for Anthropic Messages and Google Gemini generate-content APIs
- Searchable model picker with per-provider remembered selections\n- Dedicated Ollama Model Manager: browse installed models, download from the Ollama library with progress, rename (copy then remove), and delete with confirmation\n- Model discovery for supported provider APIs
- API keys masked in the settings UI; on Windows, keys are stored using Windows Credential Manager
- Endpoint and provider preferences stored locally
- Conversation history stored in the app's local settings

Provider APIs differ in their supported parameters and model capabilities. Some providers or models may not support reasoning controls, and custom servers may implement only part of the OpenAI-compatible API. Vision, tools, audio, image/file uploads, and provider-specific advanced features are not yet implemented. Model downloads, rename, and deletion are supported for Ollama only; hosted provider models are managed by their respective services.

## Provider setup

1. Open **Settings**.
2. Select the provider.
3. Confirm or edit its API endpoint.\n4. Use **Model Manager** in the sidebar to download, rename, or delete Ollama models. For hosted providers, use that provider’s website or account tools to manage access and billing.
4. Enter an API key for hosted providers (local servers usually do not need one).
5. Save, then use **Refresh** and select a model.

Default endpoints are provided for each preset. The **Custom OpenAI-compatible** option can connect to other servers that implement the models-list and streaming chat-completions endpoints.

### Supported providers and endpoints

| Provider | API style | Default endpoint |
|---|---|---|
| Ollama (local) | Ollama native | http://127.0.0.1:11434 |
| OpenAI | OpenAI-compatible | https://api.openai.com/v1 |
| OpenRouter | OpenAI-compatible | https://openrouter.ai/api/v1 |
| Groq | OpenAI-compatible | https://api.groq.com/openai/v1 |
| Together AI | OpenAI-compatible | https://api.together.xyz/v1 |
| DeepSeek | OpenAI-compatible | https://api.deepseek.com/v1 |
| Mistral | OpenAI-compatible | https://api.mistral.ai/v1 |
| Anthropic | Anthropic Messages API | https://api.anthropic.com/v1 |
| Google Gemini | Gemini API | https://generativelanguage.googleapis.com/v1beta |
| LM Studio (local) | OpenAI-compatible | http://127.0.0.1:1234/v1 |
| llama.cpp (local) | OpenAI-compatible | http://127.0.0.1:8080/v1 |
| vLLM (local) | OpenAI-compatible | http://127.0.0.1:8000/v1 |
| LocalAI | OpenAI-compatible | http://127.0.0.1:8080/v1 |
| Custom OpenAI-compatible | OpenAI-compatible | Configurable |

## Build requirements

- CMake 3.21+
- C++20 compiler (Visual Studio 2022 recommended on Windows)
- Qt 6.5+ with Widgets and Network

### Windows

Run from the repository root in a Visual Studio Developer PowerShell. Adjust the Qt path for your installation:

~~~powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.12.0/msvc2022_64"
cmake --build build --config Release
.\build\EclipseInfer.exe
~~~

Close the running app before rebuilding so Windows can replace the executable.

### Linux

Install Qt 6 Widgets/Network development packages, CMake, and a C++20 compiler:

~~~sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/EclipseInfer
~~~

## Architecture

Qt Widgets UI -> provider client -> provider-specific HTTP API.

Ollama uses its native tags and chat endpoints. OpenAI-compatible providers use a models endpoint and streaming chat completions. Anthropic and Gemini have dedicated request/stream adapters. Eclipse Infer is a client, not an inference engine: model weights and inference are supplied by the configured local server or hosted provider.

## Privacy and credentials

Conversation history and preferences are stored locally using Qt settings. On Windows, API keys are stored in Windows Credential Manager. On other platforms, the current fallback stores keys in the app's Qt settings, which is not equivalent to an OS keychain; avoid using sensitive keys on those platforms until a native keychain backend is added.

When using a cloud provider, prompts and conversation context are sent to that provider. With local providers, requests go to the configured local/network endpoint. Verify the endpoint before sending sensitive content.

## Known limitations / next steps

- No guarantee that every third-party server implements the same subset of a compatible API
- Advanced model features such as image input, tools/function calls, audio, structured outputs, and provider-specific reasoning controls
- API usage/cost estimates and rate-limit handling
- Secure native key storage on macOS/Linux
- Per-provider capability metadata and request-parameter negotiation
- Model download/management controls for non-Ollama local runtimes
- Stop-generation polish, conversation search/rename, syntax highlighting, tests, CI, and installers
