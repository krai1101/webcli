"""Isolated native CLI driver for public website regressions."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

from terminal_driver import Terminal


class PublicBrowser:
    def __init__(self, executable):
        self.executable = str(Path(executable).resolve())
        self.temporary = None
        self.worker = None
        self.terminal = None
        self.commands = 0
        self.last_action = ''

    def __enter__(self):
        self.temporary = tempfile.TemporaryDirectory()
        root = self.temporary.name
        environment = dict(os.environ, TEMP=root, TMP=root, TMPDIR=root)
        try:
            self.worker = subprocess.Popen([self.executable, '--worker'], env=environment,
                                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            directory = 'webcli' if os.name == 'nt' else f'webcli-{os.getuid()}'
            log = Path(root) / directory / 'logs/worker.log'
            deadline = time.monotonic() + 10
            while not (log.exists() and 'browser worker ready' in log.read_text(errors='replace')):
                assert self.worker.poll() is None and time.monotonic() < deadline, 'worker not ready'
                time.sleep(.01)
            self.terminal = Terminal(self.executable, environment)
            return self
        except BaseException:
            self.__exit__(None, None, None)
            raise

    def __exit__(self, *unused):
        try:
            if self.terminal is not None:
                self.terminal.close()
        finally:
            if self.worker is not None:
                if self.worker.poll() is None:
                    self.worker.terminate()
                self.worker.wait(timeout=10)
            if self.temporary is not None:
                self.temporary.cleanup()

    def run(self, *arguments):
        self.commands += 1
        self.last_action = ' '.join(arguments[:3] if arguments[0] == 'wait' else arguments[:2])
        result = self.terminal.run([*arguments, '--json', '--offset', '0', '--limit', '200'])
        page = json.loads(result.stdout)
        assert result.returncode == 0 and not page.get('error'), page
        assert not page.get('scriptErrors'), page.get('scriptErrors')
        return page

    def action(self, page, name):
        for group in ('links', 'buttons'):
            for item in page[group]:
                if item['name'].casefold() == name.casefold():
                    return self.run('click', item['ref'])
        raise AssertionError(f'No current action named {name!r}')

    def fill(self, page, name, value):
        field = next((item for item in page['fields'] if item['name'].casefold() == name.casefold()), None)
        assert field is not None, f'No current field named {name!r}'
        return self.run('fill', field['ref'], value)

    @staticmethod
    def same_document(page, document):
        assert page['documentId'] == document, 'SPA action reloaded the document'
