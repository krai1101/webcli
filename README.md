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
- Navigation, form filling, selection and Enter/Escape keyboard interaction.
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
On Linux/macOS, open a new terminal afterward. Run the same command to update.

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
webcli> observe
webcli> help
webcli> quit
```

| Command | Purpose |
| --- | --- |
| `open <url>` | Navigate to a page |
| `observe` | Read the live page without reloading |
| `click <ref>` | Activate a link or button |
| `fill <I-ref> <value>` | Fill a field; quote values containing spaces |
| `select <S-ref> <value>` | Choose an option by its value |
| `keyboard <ref> Enter\|Escape` | Send a key to a control |
| `wait [idle\|text\|gone\|ref\|selector] [value]` | Wait for a condition |

References identify links (`L1`), buttons (`B1`), inputs (`I1`) and selects
(`S1`). Use the references returned by the current page. Follow the displayed
`More: observe ...` command to read the rest of a long page. `help` shows full
syntax; `observe --debug` shows detailed warnings. `observe --offset 0` includes
header/navigation content. `close` closes the current page; `quit` exits the
prompt. Saved cookies and storage remain.

For agents, start `webcli --json` and send one command per input line.
Browsing commands return one JSON object per line without a banner or prompt.
`help` returns plain text.
Separate terminals have independent pages but share saved cookies and storage.

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

## Build from source

WebCLI requires a C++17 compiler, CMake 3.21+, Python 3.9+ and vcpkg.
Dependencies include Lexbor, QuickJS-NG, libcurl, libuv, libpsl, ICU and SQLite.

For Windows, Linux and macOS setup, build commands and testing, see
[CONTRIBUTING.md](CONTRIBUTING.md#build-from-source).

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
