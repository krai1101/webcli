# Unchanged Lightpanda demo tests

The eight scripts and golden JSON files under `lightpanda/agent/` are copied
byte for byte from `lightpanda-io/demo` commit
`24aaecefc5ced8896aaed9d3846e3049f96f93f8`. The original Go fixture server is
also preserved. `manifest.json` records their SHA-256 hashes; the runner rejects
modified copies. The upstream Apache-2.0 license is included.

These tests use the original fixture pages and `http://127.0.0.1:1234` URLs,
as Lightpanda's deterministic suite does. They are distinct from WebCLI's
additional public website workflows in `tests/lightpanda_demos.py`.

## Run

1. Clone `https://github.com/lightpanda-io/demo.git` into
   `references/lightpanda-demo` and check out the commit above. The runner checks
   its commit and rejects modified public fixtures. On Windows, use
   `git -c core.autocrlf=false clone ...` to preserve upstream line endings.
2. Configure a **separate build directory**, using your normal toolchain and
   dependency options plus `-DWEBCLI_UPSTREAM_TEST_DRIVER=ON -DBUILD_TESTING=OFF`.
3. Build target `webcli`. Its output is `webcli-upstream-driver`, not the product
   CLI. Never install this test executable as WebCLI.
4. With Go 1.23+ on PATH, run:

   ```sh
   python tests/upstream_tests.py /path/to/webcli-upstream-driver
   ```

Use `--server /path/to/demo-runner` to supply a prebuilt original Go server.
The upstream server uses Unix process APIs and cannot build unchanged on Windows.
Run it in WSL with `demo-runner -serve -http-dir /path/to/demo/public`, then use
`--external-server` with the Windows driver. This keeps the fixture server unchanged.
Use `--case magic8ball` to select one script. Port 1234 must be available.
Each script gets isolated temporary cookie/storage state.

## Adapter boundary

The test driver executes the original scripts in a separate QuickJS context.
`Page.goto`, waits, fill, selection and clicks use WebCLI's existing browsing
instance and action code. CSS selectors are resolved to existing native action
references. `Page.setChecked` uses WebCLI's existing checkbox/radio fill action;
the original event-order assertion still applies and can fail.

`adapter.js` implements only the extraction schema operations those scripts
use. It reads the actual document DOM, including selectors, attributes, array
limits and relative URLs. `evaluate` returns strings as Lightpanda's API does.
No page polyfills or replacement fixture responses are injected. Waits use
Lightpanda's 5-second default for loaded documents. Navigation and click
readiness still run through WebCLI's existing engine; this adapter does not
establish identical timeout or scheduling behavior between engines.

The returned JSON is compared exactly against upstream goldens (object key
order is immaterial). Failures return a nonzero exit status. There are no
expected-failure exemptions, automatic golden updates or assertion removals.
The suite tests the engine; it does not replace CLI/output regressions.
