# WebCLI

[![Build and test](https://github.com/krai1101/webcli/actions/workflows/build.yml/badge.svg)](https://github.com/krai1101/webcli/actions/workflows/build.yml)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![Release](https://img.shields.io/github/v/release/krai1101/webcli?include_prereleases)](https://github.com/krai1101/webcli/releases)

```text
╔═══════════════════════════════════════════════════════════════════╗
║                                                                   ║
║   ██╗    ██╗███████╗██████╗  ██████╗██╗     ██╗                   ║
║   ██║    ██║██╔════╝██╔══██╗██╔════╝██║     ██║                   ║
║   ██║ █╗ ██║█████╗  ██████╔╝██║     ██║     ██║                   ║
║   ██║███╗██║██╔══╝  ██╔══██╗██║     ██║     ██║                   ║
║   ╚███╔███╔╝███████╗██████╔╝╚██████╗███████╗██║                   ║
║    ╚══╝╚══╝ ╚══════╝╚═════╝  ╚═════╝╚══════╝╚═╝                   ║
║                                                                   ║
║   CLI browser for AI agents                                       ║
║                                                                   ║
╚═══════════════════════════════════════════════════════════════════╝
```

WebCLI is a lightweight CLI browser for AI agents, built in C++. It presents
web pages as readable text with numbered links, buttons and fields. Its native
engine runs HTML and JavaScript without Chromium or Playwright.

> **Early release:** Website support varies. Report incomplete pages and
> interactions with the URL, commands used and relevant diagnostics.

## The repository

This repository contains the application source, tests, build tooling and
release workflow for WebCLI.

## Features

- Readable page content with actionable references and pagination.
- Navigation, form filling, selection and keyboard interaction.
- JavaScript, modules, events and asynchronous page updates.
- Saved cookies and website storage.
- Independent live pages in separate terminals.
- Text or JSON output, detailed diagnostics and rotating logs.

## Downloads

Install the latest release without building from source:

**Linux x64 / macOS Apple Silicon:**

```sh
curl -fsSL https://raw.githubusercontent.com/krai1101/webcli/main/scripts/install.sh | bash
```

**Windows (PowerShell):**

```powershell
irm https://raw.githubusercontent.com/krai1101/webcli/main/scripts/install.ps1 | iex
```

The installers verify the release checksum and install under your user account.
On Linux/macOS, open a new terminal afterward.

For installations made with these scripts, run from your terminal:

```sh
webcli update
webcli uninstall
```

Updates verify the release checksum and switch to the latest stable version.
Open a new terminal afterward on Windows to refresh PATH. Uninstall keeps saved
cookies and storage. Windows removes WebCLI's installation entries from your
user PATH; Linux/macOS remove its executable link and keep the shared
`~/.local/bin` PATH configuration. Extracted ZIPs and source builds must be
updated or removed manually. Quit running WebCLI prompts before uninstalling.

Windows x64, Linux x64 and macOS Apple Silicon ZIP packages are published on the
[Releases page](https://github.com/krai1101/webcli/releases) after all three
platforms pass the build and package checks.

Extract the entire package and add its folder to PATH. Keep bundled libraries
beside the executable. Windows requires the Visual C++ x64 Redistributable;
Linux packages target Ubuntu 22.04; macOS requires 14 or later. macOS packages
are ad hoc signed, not notarized. On Linux and macOS, use `chmod +x webcli`
if the extractor removes executable permissions.

## Usage

Run `webcli` to start the interactive prompt:

```text
webcli> open https://www.python.org
webcli> open https://docs.python.org/3/
webcli> back
webcli> forward
webcli> observe
webcli> help
webcli> quit
```

| Command | Purpose |
| --- | --- |
| `open <url>` | Navigate to a page |
| `observe` | Read the live page without reloading |
| `click <ref>` | Activate a link or button and return the resulting page |
| `fill <I-ref> <value>` | Fill a field; quote values containing spaces |
| `select <S-ref> <value>` | Choose an option by its value |
| `keyboard <ref> <key>` | Send Enter, Escape, Tab or Shift+Tab to a control |
| `wait [idle\|text\|gone\|ref\|selector] [value]` | Wait for a condition |
| `back` / `forward` | Move through history and show the resulting page |

For checkboxes and radios, `fill <I-ref> true|false` sets the desired checked
state through normal activation. An already-matching state is unchanged;
canceled activation reports an error. A checked radio cannot be cleared directly.

`back` and `forward` return the resulting page and support website history
changes such as SPA routes and URL fragments. At either end of history, the
current page is returned unchanged. Both commands are available at the prompt
and as terminal commands, for example `webcli back --json`.
If no page is open, they return `NO_PAGE`.

`open` waits for the normal document load lifecycle, with a 25-second deadline.
Pages can continue changing afterward. If a referenced node is replaced, WebCLI
can recover a unique matching control in the same document and frame; links
must also retain their destination URL. Ambiguous or changed targets stay stale.
Internal processing and failed waits preserve the last returned reference
descriptions. A successful observation replaces them; use its current refs.
Reference numbers are not reused across navigation within a live instance.

`click` waits for document load and 500 ms without pending network work before
returning page text. The idle window follows Lightpanda and agent-browser;
automatic waiting after clicks is WebCLI's CLI behavior. The total readiness
deadline is 25 seconds. A timeout does not undo or retry the click; use `observe`
before acting again. Pages can schedule later updates or poll indefinitely, so
use `wait text "expected text"` when a workflow requires a specific result.
Interactions process an event-loop turn before resolving their targets.
Recurring JavaScript tasks do not block action dispatch or reset network idle.

References identify links (`L1`), buttons (`B1`), inputs (`I1`) and selects
(`S1`). Use the references returned by the current page. Follow the displayed
`More: observe ...` command to read the rest of a long page. `help` shows full
syntax; `observe --debug` shows recent action/event delivery, document fetch/XHR
results, pending work and JavaScript errors. Its trace retains the latest 128
metadata entries and writes new entries to the rotating diagnostic log when
requested. Request headers, bodies and URL query strings are excluded from this
trace; unchanged DOM output does not establish that a remote action failed.
`observe --offset 0` includes
header/navigation content. `close` closes the current page; `quit` exits the
prompt. Saved cookies and storage remain.

For agents, start `webcli --json` and send one command per input line.
Browsing commands return one JSON object per line without a banner or prompt.
`help` returns plain text.
Separate terminals have independent pages but share saved cookies and storage.
Website requests and JavaScript use a Chrome-compatible browser identity.

Optional engine settings can be supplied through environment variables before
starting WebCLI:

| Variable | Effect |
| --- | --- |
| `WEBCLI_BLOCK_PRIVATE_NETWORKS=1` | Block connections to loopback, private and link-local IP addresses. Uses direct connections, ignoring proxy settings. Default: off. |
| `WEBCLI_JS_MAX_HEAP_MB=512` | Set a QuickJS allocation limit in MiB per runtime, including dedicated workers and module compilation. Frames share their parent's runtime. Unset or `0`: engine default. |

The heap setting does not limit native DOM allocations or total process memory.

## Supported scope

WebCLI implements a subset of browser APIs for text browsing and forms. Some
JavaScript applications remain unsupported. It has no graphical renderer,
canvas rendering or file uploads. Sites may require verification or deny
access; loading a sign-in page does not establish login compatibility.

Default readable/writable streams, transform callbacks and stream piping are
supported. Byte-stream/BYOB readers and transferable streams are unsupported.
`FileList` and empty `input.files` collections support form-library checks;
local file selection and uploads remain unsupported.

Script-opened windows appear in `observe` with a `Window:` heading and use the
same control references. Message channels support cloned messages and port
transfer between iframe/window realms; worker transfers and nested transfer
lists remain unsupported. Form validity supports required values, email/URL,
patterns, numeric bounds and custom errors; it is not full constraint validation.
Element measurements are estimates for nonvisual page logic, not rendered layout.

Native dialogs support opening, closing, Escape close requests, stacked opening
order and local `method="dialog"` form completion. Close requests respect
`closedby` and cancellation. Custom dialogs use the page's JavaScript and supported CSS visibility;
hidden or `aria-hidden` content is omitted. Popover controls expose their open
content. Within each document, native modal focus remains within the top modal: initial autofocus,
Tab/Shift+Tab cycling and focus restoration on close are supported. Tab's native
default traversal currently applies to native modal dialogs; other pages can
handle the key in JavaScript. Graphical layout is not implemented.

While a native modal or visible ARIA modal is open, observation shows its content
and controls instead of the underlying page. Its `Outside dialog` button ref
sends an outside interaction to the containing DOM surface while preserving
focus for the page's dismissal/restoration handlers. The page decides whether
that dismisses a custom modal. Native dialogs dismiss this way only with
`closedby="any"`, subject to their cancel handler. This is nonvisual interaction,
not coordinate-based backdrop hit testing.

## Build from source

WebCLI requires a C++17 compiler, CMake 3.21+, Python 3.9+ and vcpkg.
Dependencies include Lexbor, QuickJS-NG, libcurl, libuv, libpsl, ICU and SQLite.

For Windows, Linux and macOS setup, build commands and testing, see
[CONTRIBUTING.md](CONTRIBUTING.md#build-from-source).

WebAssembly uses a WAMR interpreter. Function imports and exported memory,
tables and globals are supported; importing memory, tables or globals is not.
WASI, SIMD and shared-memory threads are unsupported. Geometry and hit testing
use nonvisual estimates and do not provide pixel-accurate CSS layout.

## Contributing

Bug reports and focused pull requests are welcome. Read
[CONTRIBUTING.md](CONTRIBUTING.md) before submitting a change.

## Security

WebCLI is not a hardened OS sandbox. Read [SECURITY.md](SECURITY.md) before
using authenticated or untrusted pages. Report vulnerabilities privately;
do not include credentials, cookies or private page content in public issues.

## License and notices

Copyright 2026 [krai1101](https://github.com/krai1101).
WebCLI is available under the [Apache License 2.0](LICENSE). Third-party
licenses, attribution and redistribution information are in [NOTICE](NOTICE),
[THIRD_PARTY.md](THIRD_PARTY.md) and `licenses/`.
