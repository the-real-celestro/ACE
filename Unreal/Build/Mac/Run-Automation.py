#!/usr/bin/env python3
"""Run Mac automation with a normal engine shutdown, retaining failed test results."""
import argparse
import json
from pathlib import Path
import re
import signal
import subprocess
import sys
import time


def startup_command(command, platform):
    command = list(command)
    if platform == 'darwin' and Path(command[0]).name in ('ACUnreal', 'UnrealEditor', 'UnrealEditor-Cmd'):
        # UE 5.8 disables/clears its early memory tracker during Engine::Init.
        # Cocoa can still have an allocator tag scope open on the main thread;
        # keeping tracking enabled prevents PopTag from using cleared state.
        if any(arg.lower() == '-nollm' for arg in command):
            raise ValueError('Mac automation keeps the startup memory tracker enabled; remove -nollm.')
        if not any(arg.lower() == '-llm' for arg in command):
            command.append('-llm')
    return command


# Only signal the process we launched. Unreal handles the first SIGTERM as a normal Quit.
def run_automation(command, log, expected, timeout=420, shutdown_timeout=60):
    if any('softquit' in arg.lower() or arg.lower().startswith('-testexit') for arg in command):
        raise ValueError('Do not use SoftQuit or TestExit; failed SoftQuit runs mark the engine critical on Mac.')
    command = startup_command(command, sys.platform)
    log = Path(log)
    engine_log = log.with_suffix('.engine.log')
    log.parent.mkdir(parents=True, exist_ok=True)
    engine_log.write_text('')
    started = time.monotonic()
    normal_shutdown = False
    timed_out = False
    with log.open('w') as output:
        process = subprocess.Popen([*command, '-ABSLOG=' + str(engine_log)], stdout=output, stderr=subprocess.STDOUT)
        while process.poll() is None:
            text = log.read_text(errors='replace') + engine_log.read_text(errors='replace')
            complete = 'Report can be opened in the editor at' in text
            timed_out = time.monotonic() - started >= timeout
            if complete or timed_out:
                # Avoid Automation SoftQuit: it turns test failures into GIsCriticalError,
                # which skips graphics cleanup and causes a second crash during teardown.
                process.send_signal(signal.SIGTERM)
                normal_shutdown = True
                try:
                    process.wait(timeout=shutdown_timeout)
                except subprocess.TimeoutExpired:
                    raise RuntimeError('The test process did not finish normal shutdown; no force-kill or further launch was attempted.')
                break
            time.sleep(.2)
    text = log.read_text(errors='replace') + engine_log.read_text(errors='replace')
    passed = sorted(set(re.findall(r'Result=\{Success\} Name=\{([^}]+)\}', text)))
    failed = sorted(set(re.findall(r'Result=\{Fail\} Name=\{([^}]+)\}', text)))
    bad_shutdown = any(marker in text for marker in ('Fatal error', 'Assertion failed', 'Ensure condition failed', 'Setting GIsCriticalError'))
    # UI-interaction checks can exercise the real Quit button before the runner signals.
    normal_exit = normal_shutdown or 'LogInit: Display: PreExit Game.' in text
    success = normal_exit and not timed_out and process.returncode == 0 and not failed and not bad_shutdown and len(passed) == expected
    return dict(success=success, exit_code=process.returncode, passed=passed, failed=failed,
                normal_shutdown_requested=normal_shutdown, normal_shutdown_verified=normal_exit,
                timed_out=timed_out, bad_shutdown=bad_shutdown,
                log=str(log), engine_log=str(engine_log))


# Keep command arguments structured; never execute test names or profile paths through a shell.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--expected', type=int, required=True)
    parser.add_argument('--timeout', type=float, default=420)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('Provide the test executable and arguments after --.')
    result = run_automation(command, args.log, args.expected, args.timeout)
    print(json.dumps(result, indent=2), flush=True)
    return 0 if result['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
