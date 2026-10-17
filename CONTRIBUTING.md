# Contributing to WebCLI

Bug reports and focused pull requests are welcome. Open an issue before starting
substantial work so the design and scope can be agreed first.

Keep changes within the documented [runtime scope](README.md#supported-scope).
General engine fixes are preferred over website-specific rules.

## Build from source

Requires C++17, CMake 3.21+, Python 3.9+ and [vcpkg](https://github.com/microsoft/vcpkg).
The manifest pins the dependency baseline. Run the commands below from the
WebCLI repository root after cloning:

```sh
git clone https://github.com/krai1101/webcli.git
cd webcli
```

### Windows

Install Visual Studio Build Tools with the Desktop development with C++ workload,
CMake and Python. In PowerShell:

```powershell
$env:VCPKG_ROOT = Join-Path $HOME 'vcpkg'
git clone https://github.com/microsoft/vcpkg.git $env:VCPKG_ROOT
$baseline = (Get-Content vcpkg.json | ConvertFrom-Json).'builtin-baseline'
git -C $env:VCPKG_ROOT checkout $baseline
& "$env:VCPKG_ROOT/bootstrap-vcpkg.bat" -disableMetrics
cmake -S . -B build "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
.\build\Release\webcli.exe
```

### Linux and macOS

On Ubuntu, install the build prerequisites:

```sh
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build python3 pkg-config autoconf autoconf-archive automake libtool curl zip unzip tar patchelf
```

On macOS, install Xcode command line tools and the build prerequisites:

```sh
xcode-select --install
brew install cmake ninja python pkg-config autoconf autoconf-archive automake libtool
```

Then configure and build. Use `arm64-osx` instead of `x64-linux` on Apple Silicon:

```sh
export VCPKG_ROOT="$HOME/vcpkg"
git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
baseline=$(python3 -c 'import json; print(json.load(open("vcpkg.json"))["builtin-baseline"])')
git -C "$VCPKG_ROOT" checkout "$baseline"
"$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
export VCPKG_MAX_CONCURRENCY=2
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux -DVCPKG_OVERLAY_TRIPLETS="$PWD/cmake/triplets"
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/webcli
```

If you already have a vcpkg checkout, reuse it and skip the clone. The first
dependency build can take a while; later builds reuse installed packages.
Set `-DBUILD_TESTING=OFF` only for a build without the offline checks.

## Before opening a pull request

- Build the changed code and run the CTest checks above.
- Use the repository `.clang-format` for changed C++ code.
- Keep fixtures local and deterministic; account credentials are not test inputs.
- Describe the defect, fix, verification and remaining limits.
- Update public command syntax and support claims when behavior changes.

## Code guidelines

- Maintain RAII ownership, bounded allocations and explicit cancellation.
- JavaScript access must stay on its owning thread.
- Cover native boundaries, including calls that could bypass JavaScript wrappers.
- Keep engine code independent of CLI formatting and output headers.
- Record dependency versions, licenses and redistribution requirements in
  [THIRD_PARTY.md](THIRD_PARTY.md).
- Keep credentials, private data, build outputs and reference checkouts out of Git.

See [source layout](#source-layout) for module responsibilities.
There is one product executable; engine/CLI separation is in code.

## Source layout

- `src/cli/`: commands, interactive prompt and text/JSON output.
- `src/host/`: resident IPC and instance workers.
- `src/runtime/`: DOM, JavaScript, browser APIs and scheduling.
- `src/observation/`: readable content and actionable references.
- `src/network/`: transport, origin checks and cookies.
- `src/storage/`: profile persistence and diagnostics.
- `src/core/`: shared types, text and URL utilities.

`cmake/` contains build/packaging support; `tests/` contains offline fixtures
and an optional public SPA regression.

### Public SPA regression

Run `python tests/public_spa.py /path/to/webcli` with internet access. This
exercises the public TodoMVC React app twice: create tasks, toggle completion,
filter Active/Completed without reloading the document, clear completed tasks,
and reopen. It uses a temporary profile and never modifies your saved login.
The current React demo does not persist tasks, so this checks reopening rather
than localStorage persistence. External availability can affect this test;
offline engine checks remain the default CI gate.
Run `python tests/public_demos.py /path/to/webcli` to exercise RealWorld and
React-admin. Append `realworld` or `react-admin` to select one scenario. RealWorld
checks feed/article/profile navigation, tag filtering and login/register form
editing without submitting credentials or creating accounts. React-admin checks
demo login, customer search/detail views and product/order navigation; it never
saves edits. Both use temporary profiles and fail on missing APIs or warnings.
The React-admin scenario checks demo login, customer search and navigation to
customer detail, poster and order views. It does not save customer changes or
place orders. A completed run establishes only the assertions in that scenario.

Run `python tests/public_modals.py /path/to/webcli` to open and dismiss the
Chrome native-dialog sample and Bootstrap's JavaScript modal. It checks modal
background isolation, Close, outside dismissal, Escape and reopening, including
the static modal's refusal to dismiss on outside clicks or Escape. It follows
observation pagination to find the live controls on the documentation page.
Keyboard checks use `wait idle` to let the page finish its dismissal transition.
Append `x` to check X's public onboarding modal instead: one synthetic email
fill followed by Continue must reveal the modal in the click response, conceal
the background and allow the outside action to dismiss it.
It does not submit the modal's login form or verify email delivery or authentication.
Offline dialog checks also cover form completion, input editing, cancellation,
`closedby`, stacked opening order, custom visibility and popover activation in
`tests/engine_cycle.py`.
Native focus checks cover initial autofocus, background focus blocking,
Tab/Shift+Tab wrapping and cancellation, nested restoration, and nonmodal close
restoration in `tests/runtime_contracts.py`. The engine-cycle workflow also sends
Tab and Shift+Tab through the actual CLI.

Outside activation is a nonvisual, virtual action: it selects an empty presentation backdrop,
otherwise a dialog wrapper with a registered pointer/mouse-down handler, or the
containing element. It dispatches one input sequence and leaves dismissal to
the page and preserves focus for its dismissal/restoration handlers. It cannot
promise accurate hit testing for arbitrary overlay layouts. Native gestures
still apply the ordinary mousedown focus default to actual page controls.

Manual workflow dispatches can enable the public regressions to run these
checks against the unpacked release on all three platforms. Known
compatibility failures fail that optional job; they are not skipped.

Run `python tests/lightpanda_demos.py /path/to/webcli` for Lightpanda's public
Campfire Commerce, Amiibo and Magic 8-Ball demos. These check asynchronously
loaded product/review data, quantity editing, three document navigations, and
repeated iframe/worker replies through both button clicks and Enter. Expected
product and Amiibo data are read independently from the demos' public JSON.
Append `campfire`, `amiibo` or `oracle` to run one workflow. No purchases or
account operations are performed. These checks also run in the optional public
regression job; external availability can cause failures.

Offline checks additionally cover repeated parent/iframe observation updates,
event capture/target/bubble order, listener reentrancy and immediate propagation
stopping. Public workflows supplement those deterministic regressions.

### Unchanged upstream demo tests

The eight original Lightpanda demo PandaScripts and their exact golden JSON
results are retained separately from our public CLI workflows. See
[the upstream test instructions](tests/upstream/README.md) for the pinned
fixture checkout and optional test-driver build. These use the original local
fixture server, preserve upstream assertions and fail on result differences.
The test-only driver and Go server are not product dependencies or release
artifacts. They are opt-in and do not add another build to the release jobs.

## Release workflow

`.github/workflows/build.yml` builds, tests, packages and checks a relocated ZIP
on Windows x64, Linux x64 and macOS Apple Silicon. It runs for pull requests,
manual dispatches and version tags; ordinary branch pushes do not run CI.

A tag such as `v0.1.0` publishes ZIPs and SHA-256 files only after all three
platform jobs pass. The tag's base version must match CMake's project version.
Build artifacts and failure diagnostics are retained for seven days. The Unix
packages include matching LGPL dependency sources and replaceable libraries.

Contributions are submitted under the [Apache License 2.0](LICENSE). Preserve
third-party notices and identify any borrowed code or new dependencies.
