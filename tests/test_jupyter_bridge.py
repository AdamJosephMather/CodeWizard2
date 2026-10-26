"""Run with a Python environment containing jupyter_client and ipykernel.

    python -m unittest discover -s tests -p test_jupyter_bridge.py -v

Exercises the exact bridge embedded in the C++ application, using a temporary
kernelspec and working directory. No user kernelspec/settings are modified.
"""
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time
import unittest

BRIDGE = (Path(__file__).resolve().parents[1] / "jupyterbridge.h").read_text().split('R"PY(', 1)[1].split(')PY"', 1)[0]


class BridgeTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(prefix="cw-notebook-")
        root = Path(self.folder.name)
        spec = root / "kernels" / "cw-test"
        spec.mkdir(parents=True)
        (spec / "kernel.json").write_text(json.dumps({
            "argv": [sys.executable, "-m", "ipykernel_launcher", "-f", "{connection_file}"],
            "display_name": "CodeWizard test", "language": "python",
        }))
        self.env = dict(os.environ, JUPYTER_PATH=str(root), PYTHONIOENCODING="utf-8")
        self.process = subprocess.Popen([sys.executable, "-u", "-c", BRIDGE], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", env=self.env)
        self.messages = queue.Queue()
        def read():
            for line in self.process.stdout:
                self.messages.put(json.loads(line))
        self.reader = threading.Thread(target=read, daemon=True)
        self.reader.start()
        greeting = self.messages.get(timeout=15)
        self.assertEqual(greeting["event"], "kernels")
        self.assertIn("cw-test", greeting["names"])

    def tearDown(self):
        if self.process.poll() is None:
            self.send(command="shutdown")
            try:
                self.process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.reader.join(timeout=3)
        self.process.stdin.close()
        self.process.stdout.close()
        self.process.stderr.close()
        self.folder.cleanup()

    def send(self, **message):
        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()

    def execute(self, code, cell=0, kernel="cw-test"):
        self.send(command="execute", cell=cell, kernel=kernel, cwd=self.folder.name, code=code)
        return self.collect(cell)

    def collect(self, cell):
        messages = []
        while True:
            item = self.messages.get(timeout=45)
            self.assertEqual(item["cell"], cell)
            messages.append(item)
            if item["event"] in ("done", "error"):
                return messages

    @staticmethod
    def outputs(messages, kind):
        return [m["content"] for m in messages if m.get("kind") == kind]

    def test_persistent_namespace_unicode_and_execution_count(self):
        first = self.execute("answer = 40\nprint('héllo 🐍')", cell=4)
        self.assertIn("héllo 🐍", "".join(c["text"] for c in self.outputs(first, "stream")))
        second = self.execute("answer + 2", cell=6)
        result = self.outputs(second, "execute_result")[0]
        self.assertEqual(result["data"]["text/plain"], "42")
        self.assertEqual(result["execution_count"], 2)

    def test_working_directory_and_stderr(self):
        messages = self.execute("import os, sys\nprint(os.getcwd())\nprint('problem', file=sys.stderr)")
        streams = self.outputs(messages, "stream")
        self.assertTrue(any(self.folder.name in s["text"] for s in streams))
        self.assertTrue(any(s["name"] == "stderr" and "problem" in s["text"] for s in streams))

    def test_errors_and_kernel_remains_usable(self):
        messages = self.execute("1 / 0")
        self.assertEqual(self.outputs(messages, "error")[0]["ename"], "ZeroDivisionError")
        self.assertEqual(messages[-1]["event"], "done")
        self.assertTrue(self.outputs(self.execute("2 + 3"), "execute_result"))

    def test_display_clear_and_update(self):
        messages = self.execute("from IPython.display import display, clear_output\nclear_output(wait=True)\nh = display({'hello': 'world'}, display_id=True)\nh.update('updated')")
        self.assertEqual(self.outputs(messages, "clear_output"), [{"wait": True}])
        self.assertTrue(self.outputs(messages, "display_data"))
        self.assertTrue(self.outputs(messages, "update_display_data"))

    def test_missing_kernel_reports_error(self):
        messages = self.execute("print('must not execute')", kernel="cw-missing-kernel")
        self.assertEqual(messages[-1]["event"], "error")
        self.assertIn("cw-missing-kernel", messages[-1]["message"])

    def test_interrupt_and_shutdown(self):
        pid_result = self.outputs(self.execute("import os\nos.getpid()"), "execute_result")[0]
        pid = int(pid_result["data"]["text/plain"])
        # Windows defers SIGINT during some C calls (including time.sleep).
        # A Python loop exercises the kernel's interrupt path on both platforms.
        self.send(command="execute", cell=2, kernel="cw-test", cwd=self.folder.name, code="while True: pass")
        started = self.messages.get(timeout=10)
        self.assertEqual(started["kind"], "execute_input")
        self.send(command="interrupt")
        messages = self.collect(2)
        self.assertEqual(self.outputs(messages, "error")[0]["ename"], "KeyboardInterrupt")
        self.send(command="shutdown")
        self.process.wait(timeout=15)
        import psutil
        deadline = time.monotonic() + 5
        while psutil.pid_exists(pid) and time.monotonic() < deadline:
            time.sleep(0.1)
        self.assertFalse(psutil.pid_exists(pid), "Kernel must not outlive the notebook bridge")

    def test_large_output_is_bounded_and_unicode_safe(self):
        messages = self.execute("print('🐍' * 100000)")
        text = ''.join(c['text'] for c in self.outputs(messages, 'stream'))
        self.assertLess(len(text.encode('utf-8')), 263000)
        self.assertEqual(text.count('Output truncated'), 1)
        self.assertNotIn('\ufffd', text)
        self.assertTrue(self.outputs(self.execute('6 * 7'), 'execute_result'))

    def test_print_forever_can_be_interrupted_after_truncation(self):
        self.send(command='execute', cell=3, kernel='cw-test', cwd=self.folder.name,
                  code='while True: print("test")')
        messages = []
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            message = self.messages.get(timeout=30)
            messages.append(message)
            if 'Output truncated' in message.get('content', {}).get('text', ''):
                break
        else:
            self.fail('Printing loop did not reach the output limit')
        self.send(command='interrupt')
        messages += self.collect(3)
        text = ''.join(c['text'] for c in self.outputs(messages, 'stream'))
        self.assertLessEqual(text.count('\n'), 2004)
        self.assertEqual(text.count('Output truncated'), 1)
        self.assertLess(len(messages), 100)
        self.assertEqual(self.outputs(messages, 'error')[0]['ename'], 'KeyboardInterrupt')
        self.assertTrue(self.outputs(self.execute('42'), 'execute_result'))

    def test_clear_output_cannot_bypass_execution_budget(self):
        messages = self.execute("from IPython.display import clear_output\nfor i in range(400): clear_output()")
        self.assertLessEqual(len(self.outputs(messages, 'clear_output')), 256)
        self.assertEqual(sum('Output truncated' in c['text'] for c in self.outputs(messages, 'stream')), 1)

    def test_matplotlib_show_emits_png_in_output_order(self):
        import importlib.util
        if importlib.util.find_spec('matplotlib') is None:
            self.skipTest('Install matplotlib to exercise real plot output')
        messages = self.execute("import matplotlib.pyplot as plt\nprint('before plot')\nplt.plot([0, 1, 2], [0, 1, 4])\nplt.show()\nprint('after plot')")
        outputs = [m for m in messages if m.get('kind') in ('stream', 'display_data')]
        image_index = next(i for i, m in enumerate(outputs) if 'image/png' in m['content'].get('data', {}))
        self.assertIn('before plot', ''.join(m['content'].get('text', '') for m in outputs[:image_index]))
        self.assertIn('after plot', ''.join(m['content'].get('text', '') for m in outputs[image_index+1:]))
        import base64
        png = base64.b64decode(outputs[image_index]['content']['data']['image/png'])
        self.assertTrue(png.startswith(b'\x89PNG\r\n\x1a\n'))
        self.assertFalse(self.outputs(messages, 'error'))

    def test_image_display_update_and_clear(self):
        import importlib.util
        if importlib.util.find_spec('PIL') is None:
            self.skipTest('Install Pillow to exercise image updates')
        messages = self.execute("from IPython.display import display, clear_output\nfrom PIL import Image\nh = display(Image.new('RGB', (300, 120), 'red'), display_id=True)\nh.update(Image.new('RGB', (100, 80), 'green'))\nclear_output(wait=True)")
        first = self.outputs(messages, 'display_data')[0]
        update = self.outputs(messages, 'update_display_data')[0]
        self.assertEqual(first['transient']['display_id'], update['transient']['display_id'])
        self.assertNotEqual(first['data']['image/png'], update['data']['image/png'])
        self.assertEqual(self.outputs(messages, 'clear_output'), [{'wait': True}])
        import base64, io
        from PIL import Image
        with Image.open(io.BytesIO(base64.b64decode(update['data']['image/png']))) as image:
            self.assertEqual(image.size, (100, 80))
            self.assertEqual(image.getpixel((0, 0)), (0, 128, 0))

    def test_image_payload_has_separate_budget_from_streams(self):
        messages = self.execute("from IPython.display import display\ndisplay({'image/png': 'A' * 400000, 'text/plain': 'large plot'}, raw=True)\nprint('after image')")
        self.assertEqual(len(self.outputs(messages, 'display_data')[0]['data']['image/png']), 400000)
        self.assertIn('after image', ''.join(c['text'] for c in self.outputs(messages, 'stream')))

    def test_missing_client_emits_actionable_fatal(self):
        result = subprocess.run([sys.executable, "-S", "-u", "-c", BRIDGE], input="", text=True,
                                capture_output=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        message = json.loads(result.stdout)
        self.assertEqual(message["event"], "fatal")
        self.assertIn("pip install jupyter_client ipykernel", message["message"])


if __name__ == "__main__":
    unittest.main()
