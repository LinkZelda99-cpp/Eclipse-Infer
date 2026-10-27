# Eclipse Infer

A C++20 / Qt 6 desktop AI client for local models and hosted model APIs.

## Current capabilities

- Native Qt desktop interface with theme selection, saved chat history, multiline prompts (Ctrl+Enter to send), Code Mode, adjustable temperature and context window, model-thinking display where supported, and a Stop control to cancel active generation
- System-tray integration on Windows
- Streaming chat responses
- One unified model selector; provider routing happens automatically when a model is selected
- Configurable API endpoints and credentials in Settings (provider selection is only needed for initial setup)
- Local Ollama model discovery and chat
- OpenAI-compatible API support for OpenAI, OpenRouter, Groq, Together AI, DeepSeek, Mistral, LM Studio, llama.cpp server, vLLM, LocalAI, and compatible custom endpoints
- Native streaming adapters for Anthropic Messages and Google Gemini generate-content APIs
- Unified model picker: choose a discovered model across configured providers without switching providers in the main UI; per-provider endpoints, credentials, remembered selections, and cached model lists
- Automatic discovery for Ollama and common local OpenAI-compatible runtimes (LM Studio, llama.cpp, vLLM, and LocalAI); hosted model lists are refreshed when credentials are configured
- Dedicated Ollama Model Manager: browse installed models, download from the Ollama library with progress, rename (copy then remove), and delete with confirmation
- Model discovery for supported provider APIs
- API keys masked in the settings UI; on Windows, keys are stored using Windows Credential Manager
- Endpoint and provider preferences stored locally
- Conversation history stored in the app's local settings

Provider APIs differ in their supported parameters and model capabilities. Some providers or models may not support reasoning controls, and custom servers may implement only part of the OpenAI-compatible API. Vision, tools, audio, image/file uploads, and provider-specific advanced features are not yet implemented. Model downloads, rename, and deletion are supported for Ollama only; hosted provider models are managed by their respective services.

## Provider setup

1. For local Ollama use, install/run Ollama; Eclipse Infer detects installed models automatically.
2. To set up a hosted provider or another local runtime, open **Settings** once, select that provider, confirm its endpoint, and enter its API key if required.
3. Return to the chat and select the model directly. Eclipse Infer routes each request to that model's provider automatically; you do not need to switch providers in the main window.
4. Use **Model Manager** to see installed Ollama models, pick a popular model or type a model name, then choose **Add / Download model**. You can also rename or delete local Ollama models there.

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

## Updating Eclipse Infer

Use **Help → Check for Updates** to compare the installed version with the newest stable GitHub Release. The app also checks automatically at most once per day. On Windows, choosing **Download and install** downloads the signed-transport HTTPS release package, closes the app, replaces its portable application files, and restarts it. Other platforms can open the release page to install manually.

To publish an update, create and publish a GitHub Release with a semantic version tag such as `v0.2.0`. The `.github/workflows/release-windows.yml` workflow builds a portable Windows package and attaches `EclipseInfer-Windows-x64.zip` to that release. The release tag becomes the version embedded in that build. Users need a published stable release and its generated asset for in-app Windows installation to work.

Updates replace the app files in the folder where Eclipse Infer is running. If that folder requires administrator permissions, install the release manually with the appropriate permissions.

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
