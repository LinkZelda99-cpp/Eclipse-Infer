# Eclipse Infer

A C++20 and Qt 6 desktop client for Ollama, with a sleek Eclipse-inspired interface.

## Current MVP

- System-aware light/dark theme with a remembered manual override
- Local Ollama model discovery and connection status
- Streaming chat responses
- GitHub-flavored Markdown rendering, including headings, paragraphs, links, emphasis, lists, task lists, blockquotes, tables, horizontal rules, and fenced code blocks
- Styled code blocks with monospace formatting and readable table/blockquote layouts
- Main-screen thinking toggle and live display of Ollama's separate `message.thinking` stream for compatible models
- Model thinking text saved with the corresponding assistant message and restored when reopening a conversation
- Temperature and context-length controls
- Full conversation transcripts saved locally and reopened from the sidebar
- Settings dialog for appearance, default model, and saved-history limits
- Persistent temperature, context-length, and thinking preferences
- Up to 30 recent conversations by default, with a setting to clear saved history

This is an early MVP. Stop-generation UI, model installation controls, syntax highlighting, conversation search/rename, and automated tests remain future work.

## Requirements

- CMake 3.21+
- C++20 compiler (Visual Studio 2022 on Windows recommended)
- Qt 6.5+ with Widgets and Network
- Ollama running locally, with at least one model installed

## Windows build

Install Qt 6 with the MSVC kit, then run from the repository root (replace the Qt path with your installed kit):

    cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2022_64"
    cmake --build build --config Release
    build\Release\EclipseInfer.exe

## Linux build

Install Qt 6 Widgets/Network development packages, CMake, and a C++20 compiler:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j
    ./build/EclipseInfer

## Architecture

Qt Widgets UI -> Ollama HTTP API at http://127.0.0.1:11434 -> locally installed model.

Ollama handles model loading and inference. Eclipse Infer is a native client; it does not include model weights or a separate inference runtime. Chat streams are consumed as newline-delimited JSON.

## Privacy

The app sends prompts to the local Ollama server at 127.0.0.1:11434. This MVP includes no hosted inference API or analytics.

## Roadmap

- Conversation search and rename
- Stop-generation button and robust cancellation
- Rename, delete, and search conversations
- Copy message/code and regenerate responses
- Syntax-highlighted code blocks
- Pull, delete, and inspect Ollama models
- System prompt editor and per-model settings
- Image/file attachments for compatible models
- Keyboard shortcuts, accessibility, app icon, installers, CI, and tests
