# Velox

Velox is a minimal Chromium Embedded Framework browser shell for Windows. It stays intentionally lean: one window, real Chromium rendering, measurable performance hooks, and a compact custom chrome built around fast navigation instead of feature sprawl.

## Features

- Real Chromium-based rendering through CEF
- Native host window with grouped tabs, a custom-painted tab strip, and navigation controls
- Polished custom chrome with a styled omnibox shell, home button, runtime/privacy badges, a quick settings popover, status text, and load progress
- Multi-tab browsing with group accents for Focus, Build, and Chill workflows
- Drag-to-reorder tabs without spawning extra windows
- Right-side library panel for settings, history, and downloads
- Persistent bookmarks panel with keyboard-driven save/open flows
- Multi-process startup via `CefExecuteProcess` and `CefInitialize`
- Auto-tuned runtime profile that scales Chromium process limits to the current machine
- Aggressive browser-shell tuning via Chromium command-line switches for lower idle work
- Omnibox-style address field that opens URLs directly and sends plain text to a configurable search engine
- Quick search-engine switching between Google, DuckDuckGo, Bing, and Startpage, with the choice saved under the user profile
- Local history persistence under the profile directory
- Local bookmark persistence under the profile directory
- Download capture with per-session progress tracking and automatic save targets under the profile downloads folder
- Startup cache budgeting that trims oversized cache directories before Chromium starts
- Lightweight predictive warmup that remembers hot hosts and primes DNS for faster repeat visits
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
- `Ctrl+T` opens a new tab
- `Ctrl+Tab` and `Ctrl+Shift+Tab` switch tabs
- `Ctrl+H` toggles the history panel
- `Ctrl+J` toggles the downloads panel
- `Ctrl+,` toggles the settings panel
- `Ctrl+B` toggles the bookmarks panel
- `Ctrl+D` saves or removes the current page from bookmarks
- `Ctrl+R` or `F5` reloads
- `Esc` stops loading
- `Alt+Left` and `Alt+Right` navigate back/forward
- `Ctrl+W` closes the active tab, or the window if only one tab remains

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
  "search": {
    "provider_name": "Google",
    "query_url_template": "https://www.google.com/search?q={query}"
  },
  "optimization": {
    "auto_tune": true,
    "renderer_process_limit": 0,
    "predictive_warmup": true,
    "predictor_host_count": 4,
    "max_cache_size_mb": 512,
    "cache_trim_target_percent": 80
  },
  "incognito_default": false
}
```

Notes:

- `search.query_url_template` must contain `{query}` somewhere in the URL. Velox URL-encodes the typed text before inserting it.
- `optimization.max_cache_size_mb` controls when the disk cache gets trimmed on startup.
- `optimization.cache_trim_target_percent` controls how far Velox trims once the cache exceeds budget.
- `optimization.predictive_warmup` and `optimization.predictor_host_count` control the lightweight hot-host DNS warmup pass.

## How CEF Is Wired In

- `wWinMain` creates a shared `VeloxCefApp` instance and calls `CefExecuteProcess`.
- If the current process is a Chromium subprocess, CEF runs that role and exits immediately.
- The browser process loads settings, starts logging and crash handling, and then calls `CefInitialize`.
- `VeloxCefApp::OnBeforeCommandLineProcessing` applies browser-shell switches that cut background services, keep GPU acceleration on, and honor the detected runtime profile.
- `BrowserPolicy` owns request-context preferences, third-party cookie policy, cross-site referrer trimming, WebRTC privacy hardening, request header privacy signals, and ad/tracker blocking.
- `SitePredictor` stores a tiny hot-host history under the profile directory and warms the most likely hosts in the background during startup.
- `CacheMaintenance` trims old cache files before CEF spins up so long-running profiles do not quietly turn cold start into sludge.
- The native Win32 shell creates a child `CefBrowser` window for page rendering.
- The Win32 shell keeps the UI cheap: owner-drawn buttons, common-controls progress bar, and direct layout without any extra widget framework.
- The omnibox decides between URL navigation and search queries locally, then expands search terms through the configured search template.
- The chrome reserves a right-side library panel for settings, history, and download state instead of opening extra native windows.
- The profile badge and keyboard shortcuts open custom in-app surfaces for changing the search provider, reviewing recent history, opening saved pages, and checking downloads without leaving the current page.
- The renderer process injects a tiny paint observer that reports `first-paint` and `first-contentful-paint` back to the browser process via `CefProcessMessage`.

## Known Limitations

- Tabs are grouped visually, but groups are still fixed presets instead of fully custom user-named groups
- No Windows sandbox in v1
- Downloads currently auto-save to the profile downloads folder instead of prompting for a location
- Bookmarks are currently a flat saved-pages list without folders or sync
- The built-in blocker uses a curated hostname/pattern list, not a full EasyList-scale rules engine yet
- Predictive warmup currently primes DNS only; it does not keep hidden live pages or speculative renderers around
- No custom networking stack or scheme handlers yet
- No session restore, extensions, sync, accounts, or browser chrome beyond the toolbar
- ATL is not installed in the current Build Tools environment, so CEF prints an ATL warning during configure. Velox does not rely on ATL in this build.
- Velox intentionally tunes Chromium and V8 through supported CEF hooks; it does not patch Blink or V8 source directly.

## Roadmap

1. Move grouped tabs toward isolated request contexts and richer session restore.
2. Introduce Windows sandbox support and a dedicated helper subprocess if needed.
3. Expand downloads into a fuller manager with pause/resume, open-folder actions, and save prompts.
4. Add custom networking hooks for interception, metrics, and policy controls.
5. Add richer settings, history search, and bookmark folders without bloating the shell.
