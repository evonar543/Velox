# Velox

Velox is a minimal Chromium Embedded Framework browser shell for Windows. It is intentionally small: one window, one tab, real Chromium rendering, measurable performance hooks, and just enough native UI to navigate arbitrary sites.

## Features

- Real Chromium-based rendering through CEF
- Native Win32 host window with address bar and navigation buttons
- Polished native toolbar with runtime/privacy badges, status text, and load progress
- Multi-process startup via `CefExecuteProcess` and `CefInitialize`
- Auto-tuned runtime profile that scales Chromium process limits to the current machine
- Aggressive browser-shell tuning via Chromium command-line switches for lower idle work
- JSON-backed settings
- Structured file logging and crash dump generation
- Benchmark hooks for startup, navigation, first paint, and memory
- Optional incognito scaffold using a dedicated in-memory request context
- Built-in privacy defaults: `DNT`, `Sec-GPC`, third-party cookie blocking, cross-site referrer reduction, WebRTC UDP hardening, and password-manager disablement
- Sensitive site permission requests are denied by default for a quieter, privacy-first shell
- Built-in lightweight blocking for common ad and tracker hosts plus tracking-parameter stripping
- Single-tab popup collapse so sites reuse the current browser instead of spawning extra windows
- Keyboard shortcuts for faster navigation without extra chrome overhead

## Requirements

- Windows 10 or Windows 11 x64
- Visual Studio Build Tools 2026 or Visual Studio 2026 with the Windows SDK
- CMake 3.27+
- Official CEF Windows x64 standard distribution extracted locally

Tested toolchain assumptions:

- CMake `4.2.3`
- Visual Studio generator `Visual Studio 18 2026`
- CEF `144.0.16+g4231b34+chromium-144.0.7559.231` standard Windows x64 distribution

## Build

1. Download and extract a recent official CEF Windows x64 standard distribution.
2. Configure the project with `CEF_ROOT` pointing at the extracted SDK.

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCEF_ROOT=C:/path/to/cef_binary_windows_x64
cmake --build build --config Release
```

The resulting binary will be in `build/Release/velox.exe` with the required CEF runtime files and `config/settings.json` copied beside it.

## Run

```powershell
.\build\Release\velox.exe --url=https://example.com
```

Useful flags:

- `--incognito`
- `--profile-dir=profile`
- `--log-file=logs/velox.log`
- `--dump-benchmarks=logs/metrics.jsonl`
- `--quit-after-load`

Useful shortcuts:

- `Ctrl+L` or `F6` focuses the address bar
- `Ctrl+R` or `F5` reloads
- `Esc` stops loading
- `Alt+Left` and `Alt+Right` navigate back/forward
- `Ctrl+W` closes the window

Relative paths passed on the command line are resolved from the current working directory. Relative paths in `config/settings.json` are resolved from the executable directory.
The portable build is expected to launch correctly even if the current working directory is not the executable folder.

## Settings Schema

```json
{
  "startup_url": "https://example.com",
  "window": { "width": 1280, "height": 800 },
  "paths": {
    "profile_dir": "profile",
    "cache_dir": "profile/cache",
    "log_dir": "logs"
  },
  "logging": { "level": "info" },
  "benchmarking": {
    "enabled": true,
    "output": "logs/metrics.jsonl"
  },
  "privacy": {
    "do_not_track": true,
    "global_privacy_control": true,
    "block_third_party_cookies": true,
    "strip_tracking_query_parameters": true,
    "strip_cross_site_referrers": true,
    "block_webrtc_non_proxied_udp": true,
    "disable_password_manager": true,
    "block_external_protocols": true
  },
  "blocking": {
    "enabled": true,
    "block_ads": true,
    "block_trackers": true
  },
  "optimization": {
    "auto_tune": true,
    "renderer_process_limit": 0
  },
  "incognito_default": false
}
```

## How CEF Is Wired In

- `wWinMain` creates a shared `VeloxCefApp` instance and calls `CefExecuteProcess`.
- If the current process is a Chromium subprocess, CEF runs that role and exits immediately.
- The browser process loads settings, starts logging and crash handling, and then calls `CefInitialize`.
- `VeloxCefApp::OnBeforeCommandLineProcessing` applies browser-shell switches that cut background services, keep GPU acceleration on, and honor the detected runtime profile.
- `BrowserPolicy` owns request-context preferences, third-party cookie policy, cross-site referrer trimming, WebRTC privacy hardening, request header privacy signals, and ad/tracker blocking.
- The native Win32 shell creates a child `CefBrowser` window for page rendering.
- The Win32 shell keeps the UI cheap: owner-drawn buttons, common-controls progress bar, and direct layout without any extra widget framework.
- The renderer process injects a tiny paint observer that reports `first-paint` and `first-contentful-paint` back to the browser process via `CefProcessMessage`.

## Known Limitations

- Single-tab only
- No Windows sandbox in v1
- No downloads manager
- The built-in blocker uses a curated hostname/pattern list, not a full EasyList-scale rules engine yet
- No custom networking stack or scheme handlers yet
- No session restore, extensions, sync, accounts, or browser chrome beyond the toolbar
- ATL is not installed in the current Build Tools environment, so CEF prints an ATL warning during configure. Velox does not rely on ATL in this build.
- Velox intentionally tunes Chromium and V8 through supported CEF hooks; it does not patch Blink or V8 source directly.

## Roadmap

1. Add a tab strip and tab model with isolated request contexts.
2. Introduce Windows sandbox support and a dedicated helper subprocess if needed.
3. Add downloads with progress UI and safe file handling.
4. Add custom networking hooks for interception, metrics, and policy controls.
