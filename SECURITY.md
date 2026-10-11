# Security

WebCLI executes remote JavaScript in a supported native browser API subset.
It is not a hardened OS sandbox and does not claim complete browser conformance.
There are no upload commands or page APIs for reading arbitrary local files.

Native fetch checks CORS before exposing cross-origin responses, validates
preflights before unsafe cross-origin requests, filters forbidden request/response
headers, and makes no-cors responses opaque. Unsupported redirect-taint cases
fail explicitly. Cookie/storage access stays within the native document origin. libpsl validates
public suffixes for JavaScript cookies and libcurl validates HTTP cookies with
PSL support. Startup fails if libcurl lacks PSL support or libpsl lacks built-in
suffix data.

Embedded frames have separate documents and JavaScript contexts. Same-origin
checks protect frame document/global access; cross-origin messaging checks the
target origin and clones its payload. Response X-Frame-Options and the supported
frame-ancestors sources are checked against every ancestor before parsing.
Sandbox restrictions include author scripts, forms and opaque origins. This is
a partial implementation, not full CSP/sandbox conformance or process isolation.

Dedicated workers use separate JavaScript runtimes and native threads in the
same process. Entry scripts require same-origin HTTP(S) or a Blob URL owned by
the creating realm. Classic script imports restrict cookies to the inherited
origin; fetch/XHR use the normal origin and credential checks. Termination and
committed cross-document navigation cancel worker execution and requests.
Workers provide neither process isolation nor full CSP enforcement.

Separate live instances share the saved default cookies/localStorage profile.
Do not treat independent terminals as separate identities or tenants. Profile
files contain authentication state and are not encrypted by WebCLI. Runtime
logs can include page text/errors and URLs; they rotate by size and age.

Submitting forms or sending requests can change remote state. Interrupted unsafe
requests report an uncertain outcome; cancellation does not undo a submission.

For vulnerabilities, contact the repository maintainer privately through GitHub
Security Advisories once private reporting is enabled. Do not put credentials,
private page content or authentication cookies into public bug reports.
