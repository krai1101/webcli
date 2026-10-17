"""Replay pinned, unmodified Lightpanda demo scripts and compare exact goldens.

Requires the test-only driver, Go 1.23+ (or --server), and the pinned demo checkout.
No product commands, test assertions, expected results or fixture pages are rewritten.
"""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import tarfile
import time
from urllib.request import urlopen


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('driver', type=Path)
    parser.add_argument('--demo-root', type=Path, default=Path('references/lightpanda-demo'))
    parser.add_argument('--server', type=Path, help='prebuilt original Lightpanda demo Go runner')
    parser.add_argument('--external-server', action='store_true', help='use the original server already listening on port 1234')
    parser.add_argument('--case', help='one script name; default runs every pinned script')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent / 'upstream'
    upstream = root / 'lightpanda'
    manifest = json.loads((upstream / 'manifest.json').read_text())
    for name, checksum in manifest['files'].items():
        assert hashlib.sha256((upstream / name).read_bytes()).hexdigest() == checksum, 'Modified upstream file: ' + name
    demo = args.demo_root.resolve()
    actual_commit = subprocess.check_output(['git', '-C', str(demo), 'rev-parse', 'HEAD'], text=True).strip()
    assert actual_commit == manifest['commit'], 'Fixture checkout must match pinned upstream commit'
    # Compare actual bytes, rather than Git status: WSL and Windows report
    # different executable bits for the same files on a shared NTFS checkout.
    archive = subprocess.check_output(['git', '-c', 'core.autocrlf=false', '-c', 'core.eol=lf',
                                       '-C', str(demo), 'archive', '--format=tar', manifest['commit'], 'public'])
    with tarfile.open(fileobj=io.BytesIO(archive)) as fixtures:
        names = set()
        for member in fixtures:
            if not member.isfile():
                continue
            names.add(member.name)
            assert (demo / member.name).read_bytes() == fixtures.extractfile(member).read(), 'Modified upstream fixture: ' + member.name
    actual_names = {path.relative_to(demo).as_posix() for path in (demo / 'public').rglob('*') if path.is_file()}
    assert actual_names == names, 'Files were added to or removed from upstream public fixtures'
    scripts = sorted((upstream / 'agent/scripts').glob('*.js'))
    if args.case:
        scripts = [script for script in scripts if script.stem == args.case]
        assert scripts, 'Unknown upstream case: ' + args.case
    failures = []
    with tempfile.TemporaryDirectory(prefix='webcli-upstream-') as temporary:
        temporary = Path(temporary)
        server = args.server.resolve() if args.server else temporary / ('demo-runner.exe' if os.name == 'nt' else 'demo-runner')
        if not args.server and not args.external_server:
            assert shutil.which('go'), 'Go 1.23+ is required to build the unmodified fixture server'
            subprocess.run(['go', 'build', '-o', str(server), 'main.go'], cwd=upstream / 'runner', check=True)
        log = (temporary / 'server.log').open('w+')
        process = None if args.external_server else subprocess.Popen(
            [str(server), '-serve', '-http-addr', '127.0.0.1:1234', '-http-dir', str(demo / 'public')],
            stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 10
            while True:
                assert process is None or process.poll() is None, 'Original fixture server exited; check port 1234 is free'
                try:
                    with urlopen('http://127.0.0.1:1234/campfire-commerce/json/product.json', timeout=.5):
                        break
                except OSError:
                    if time.monotonic() >= deadline:
                        raise RuntimeError('Original fixture server did not become ready')
                    time.sleep(.05)
            for script in scripts:
                profile = temporary / script.stem
                profile.mkdir()
                environment = dict(os.environ, TEMP=str(profile), TMP=str(profile), TMPDIR=str(profile))
                try:
                    result = subprocess.run([str(args.driver.resolve()), str(root / 'adapter.js'), str(script)],
                                            env=environment, text=True, encoding='utf-8', capture_output=True, timeout=130)
                    try:
                        output = json.loads(result.stdout)
                    except ValueError as error:
                        raise AssertionError({'exit': result.returncode, 'stdout': result.stdout,
                                              'stderr': result.stderr}) from error
                    expected = json.loads((upstream / 'agent/golden' / (script.stem + '.json')).read_text())
                    assert result.returncode == 0 and output.get('ok'), (output, result.stderr)
                    assert output['value'] == expected, {'expected': expected, 'actual': output['value']}
                    print('PASS unchanged upstream ' + script.stem, flush=True)
                except (AssertionError, ValueError, subprocess.TimeoutExpired) as error:
                    failures.append(script.stem)
                    print('FAIL unchanged upstream ' + script.stem + ': ' + str(error), flush=True)
        finally:
            if process is not None:
                process.terminate()
                process.wait(timeout=10)
            log.close()
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main())
