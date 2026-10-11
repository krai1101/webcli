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

`cmake/` contains build/packaging support; `tests/` contains offline fixtures.

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
