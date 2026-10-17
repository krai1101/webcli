"""Public TodoMVC SPA regression; needs internet access, no local substitute.

Usage: python tests/public_spa.py /path/to/webcli [TodoMVC URL]
Uses a temporary profile and verifies observable task state, not just commands.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from terminal_driver import Terminal

exe = str(Path(sys.argv[1]).resolve())
url = sys.argv[2] if len(sys.argv) > 2 else 'https://todomvc.com/examples/react/dist/'

with tempfile.TemporaryDirectory() as temporary:
    environment = dict(os.environ, TEMP=temporary, TMP=temporary, TMPDIR=temporary)
    worker = subprocess.Popen([exe, '--worker'], env=environment,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    terminal = None
    try:
        directory = 'webcli' if os.name == 'nt' else f'webcli-{os.getuid()}'
        log = Path(temporary) / directory / 'logs/worker.log'
        deadline = time.monotonic() + 10
        while not (log.exists() and 'browser worker ready' in log.read_text(errors='replace')):
            assert worker.poll() is None and time.monotonic() < deadline, 'worker not ready'
            time.sleep(.01)
        terminal = Terminal(exe, environment)

        def run(*arguments):
            result = terminal.run([*arguments, '--json', '--offset', '0', '--limit', '200'])
            page = json.loads(result.stdout)
            assert result.returncode == 0 and not page.get('error'), page
            assert not page.get('scriptErrors'), page.get('scriptErrors')
            return page

        def link(page, name):
            return next(item['ref'] for item in page['links'] if item['name'] == name)

        for cycle in range(2):
            first, second = f'WebCLI completed task {cycle}', f'WebCLI active task {cycle}'
            page = run('open', url)
            document = page['documentId']
            for task in [first, second]:
                entry = next(item['ref'] for item in page['fields'] if item['type'] == 'text')
                run('fill', entry, task)
                page = run('keyboard', entry, 'Enter')
                page = run('observe')
                assert task in page['text'], page
            checkbox = next(item['ref'] for item in page['fields']
                            if item['type'] == 'checkbox' and 'all' not in item['name'].lower())
            page = run('click', checkbox)
            assert first in page['text'] and second in page['text'], page
            page = run('click', link(page, 'Active'))
            assert first not in page['text'] and second in page['text'], page
            assert page['documentId'] == document, 'filter caused a document reload'
            page = run('click', link(page, 'Completed'))
            assert first in page['text'] and second not in page['text'], page
            clear = next(item['ref'] for item in page['buttons'] if item['name'] == 'Clear completed')
            page = run('click', clear)
            assert first not in page['text'], page
            page = run('click', link(page, 'All'))
            assert first not in page['text'] and second in page['text'], page
            checkbox = next(item['ref'] for item in page['fields']
                            if item['type'] == 'checkbox' and 'all' not in item['name'].lower())
            page = run('click', checkbox)
            clear = next(item['ref'] for item in page['buttons'] if item['name'] == 'Clear completed')
            page = run('click', clear)
            assert first not in page['text'] and second not in page['text'], page
            page = run('open', url)
            assert first not in page['text'] and second not in page['text'], page
        print('PASS public SPA: create, toggle, hash filters, delete, reopen, repeated workflow')
    finally:
        if terminal is not None:
            terminal.close()
        if worker.poll() is None:
            worker.terminate()
        worker.wait(timeout=10)
