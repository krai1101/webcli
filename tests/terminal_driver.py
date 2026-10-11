"""Give CLI tests real, independent parent processes, like separate shells."""

import json
from pathlib import Path
import subprocess
import sys
import threading
from types import SimpleNamespace


class Terminal:
    def __init__(self, executable, environment):
        self.lock = threading.Lock()
        self.process = subprocess.Popen(
            [sys.executable, str(Path(__file__).resolve()), executable],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, encoding="utf-8", env=environment,
        )

    def run(self, arguments, input=None, timeout=160):
        return SimpleNamespace(**self._request({"args": arguments, "input": input,
                                               "timeout": timeout}))

    def run_many(self, commands, timeout=160):
        """Launch commands together with the same parent process identity."""
        return [SimpleNamespace(**result) for result in
                self._request({"commands": commands, "timeout": timeout})["results"]]

    def _request(self, request):
        with self.lock:
            self.process.stdin.write(json.dumps(request) + "\n")
            self.process.stdin.flush()
            line = self.process.stdout.readline()
            if not line:
                raise RuntimeError("Test terminal stopped: " + self.process.stderr.read())
            result = json.loads(line)
            if "exception" in result:
                raise RuntimeError(result["exception"])
            return result

    def close(self):
        if self.process.poll() is not None:
            return
        try:
            self.run(["close", "--json"])
        finally:
            self.process.stdin.close()
            try:
                self.process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
            self.process.stdout.close()
            self.process.stderr.close()


def main():
    for line in sys.stdin:
        request = json.loads(line)
        try:
            if "commands" in request:
                children = []
                try:
                    for args in request["commands"]:
                        children.append(subprocess.Popen(
                            [sys.argv[1], *args], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, encoding="utf-8"))
                    results = []
                    for child in children:
                        stdout, stderr = child.communicate(timeout=request["timeout"])
                        results.append({"returncode": child.returncode,
                                        "stdout": stdout, "stderr": stderr})
                    print(json.dumps({"results": results}), flush=True)
                finally:
                    for child in children:
                        if child.poll() is None:
                            child.kill()
                            child.communicate()
                continue
            result = subprocess.run(
                [sys.argv[1], *request["args"]], input=request["input"],
                capture_output=True, text=True, encoding="utf-8",
                timeout=request["timeout"],
            )
            response = {"returncode": result.returncode, "stdout": result.stdout,
                        "stderr": result.stderr}
        except Exception as error:
            response = {"exception": str(error)}
        print(json.dumps(response), flush=True)


if __name__ == "__main__":
    main()
