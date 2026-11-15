"""Prove failed and timed-out checks remain failures while their process closes normally."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('mac_automation', Path(__file__).with_name('Run-Automation.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class NormalAutomationExitTests(unittest.TestCase):
    # A tiny subprocess emulates the engine's graceful termination handler, without opening a game.
    def exercise(self, result, timeout=5):
        with tempfile.TemporaryDirectory(prefix='ace-normal-exit-') as folder:
            root = Path(folder)
            fixture = root / 'fixture.py'
            marker = root / 'closed-normally'
            fixture.write_text('''import pathlib,signal,sys,time
def close(signum, frame):
 pathlib.Path(sys.argv[1]).write_text('normal shutdown')
 sys.exit(0)
signal.signal(signal.SIGTERM,close)
if sys.argv[2]!='Timeout':
 print('Test Completed. Result={'+sys.argv[2]+'} Name={Fixture}',flush=True)
 print('Report can be opened in the editor at fixture',flush=True)
while True:time.sleep(.05)
''')
            record = module.run_automation([sys.executable, str(fixture), str(marker), result], root / 'run.log', 1, timeout)
            self.assertEqual(marker.read_text(), 'normal shutdown')
            self.assertEqual(record['exit_code'], 0)
            self.assertTrue(record['normal_shutdown_requested'])
            return record

    # Successful checks must finish with an ordinary process exit.
    def test_success(self):
        self.assertTrue(self.exercise('Success')['success'])

    # The runner must retain a failed assertion even though the process itself closes normally.
    def test_failed_assertion(self):
        record = self.exercise('Fail')
        self.assertFalse(record['success'])
        self.assertEqual(record['failed'], ['Fixture'])

    # A timeout requests one normal shutdown and remains a failed check.
    def test_timeout(self):
        record = self.exercise('Timeout', .3)
        self.assertFalse(record['success'])
        self.assertTrue(record['timed_out'])

    # A UI test that presses the actual Quit button can finish without an external signal.
    def test_application_quits_normally(self):
        with tempfile.TemporaryDirectory(prefix='ace-ui-exit-') as folder:
            record = module.run_automation([sys.executable, '-c',
                "print('Test Completed. Result={Success} Name={Fixture}'); print('LogInit: Display: PreExit Game.')"],
                Path(folder) / 'run.log', 1)
            self.assertTrue(record['success'])
            self.assertTrue(record['normal_shutdown_verified'])
            self.assertFalse(record['normal_shutdown_requested'])


if __name__ == '__main__':
    unittest.main()
