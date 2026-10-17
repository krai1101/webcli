# Third-party notices

WebCLI project source is licensed under Apache-2.0; see [LICENSE](LICENSE).
Dependency and data licenses remain their respective licenses. These notices
were collected from the installed dependencies used for the current build.

| Component | License | Notice |
| --- | --- | --- |
| libcurl | curl license | [text](licenses/curl.txt) |
| Lexbor | Apache-2.0 | [text](licenses/lexbor.txt) |
| QuickJS-NG | MIT and bundled component notices | [text](licenses/quickjs-ng.txt) |
| libuv | MIT and bundled component notices | [text](licenses/libuv.txt) |
| OpenSSL | Apache-2.0 and bundled component notices | [text](licenses/openssl.txt) |
| zlib (curl dependency) | zlib license | [text](licenses/zlib.txt) |
| nghttp2 (curl HTTP/2 dependency) | MIT | [text](licenses/nghttp2.txt) |
| libpsl | MIT and Chromium BSD notices | [text](licenses/libpsl.txt), [DAFSA notice](licenses/libpsl-chromium.txt) |
| ICU (Unicode segmentation; also Windows libpsl dependency) | Unicode and bundled component notices | [text](licenses/icu.txt) |
| SQLite (IndexedDB backing store) | Public domain | [notice](licenses/sqlite3.txt) |
| libidn2 (Linux/macOS libpsl dependency) | LGPL-3.0-or-later or GPL-2.0-or-later and bundled notices | [text](licenses/libidn2.txt) |
| libunistring (Linux/macOS libpsl dependency) | LGPL-3.0-or-later or GPL-2.0-or-later and bundled notices | [text](licenses/libunistring.txt) |
| Public Suffix List (built into libpsl) | MPL-2.0 | [license](licenses/public-suffix-MPL-2.0.txt), [upstream dataset](https://publicsuffix.org/list/) |

libpsl includes both ICANN and private suffix rules. The pinned vcpkg port builds
its dataset from Public Suffix List commit
[`0ed17ee161ed2ae551c78f3b399ac8f2724d2154`](https://github.com/publicsuffix/list/blob/0ed17ee161ed2ae551c78f3b399ac8f2724d2154/public_suffix_list.dat).
Update the dependency baseline to refresh the packaged suffix data.

Include this file, LICENSE and licenses/ with source and binary distributions.
Follow each dependency license when distributing binaries, including the
source and relinking requirements for statically linked LGPL components.
If build options add dependencies, include their notices as well. vcpkg build
helpers are build tools; their upstream licenses apply when distributing them.
Unix release archives include matching upstream sources and build recipes for
libidn2 and libunistring in `dependency-sources/`. These libraries remain
dynamically linked and replaceable. Other bundled dependency notices are in
`licenses/`. Operating-system runtime libraries are not bundled.

## Design acknowledgment

### Upstream test fixtures

`tests/upstream/lightpanda/` contains unmodified scripts, golden results and the
fixture server from [Lightpanda's demo repository](https://github.com/lightpanda-io/demo),
commit `24aaecefc5ced8896aaed9d3846e3049f96f93f8`, under Apache-2.0.
The upstream license and copyright notices are retained in that directory.
These are development tests, not runtime dependencies. WebCLI's test adapter
is separate; it does not change the upstream scripts or expected results.

[Andy Cufari's WebCLI](https://github.com/andycufari/webcli) informed the
numbered text interaction design and terminal wordmark. Its MIT license and
copyright notice are included in [licenses/andycufari-webcli-MIT.txt](licenses/andycufari-webcli-MIT.txt).
It is a design reference, not a runtime dependency. WebCLI's native C++ engine
is developed independently.

WAMR 2.4.5 (WebAssembly interpreter) is distributed under Apache-2.0 WITH
LLVM-exception; see [its license](licenses/wamr.txt). The build pins commit
`25bd7eb63e828e4bd242cc9b38d260b4b31c6605` and disables the standalone
memory-shrinking optimization in a generated loader copy to support host
JavaScript memory growth. WASI, JIT/AOT and shared-memory threads are disabled.
