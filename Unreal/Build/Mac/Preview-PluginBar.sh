#!/bin/bash
# Build, render-check and open the development client with the painted plugin bar.
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: bash Unreal/Build/Mac/Preview-PluginBar.sh [--test-only] [--full-checks] [--help]

Build the Mac development editor client, check the painted plugin icons with
Unreal's renderer, then open AC: Unreal for a normal login.
--test-only exits after validation instead of opening the game.
--full-checks also runs the broader HostAndPanel and KeyboardBindings suites.
Set ACUE_ENGINE_ROOT to override /Users/Shared/Epic Games/UE_5.8.
USAGE
}

pluginbar_launch=true
pluginbar_full_checks=false
for pluginbar_arg in "$@"; do
    case "$pluginbar_arg" in
        --test-only) pluginbar_launch=false ;;
        --full-checks) pluginbar_full_checks=true ;;
        --help|-h) usage; exit 0 ;;
        *) printf 'Unknown argument: %s\n' "$pluginbar_arg" >&2; usage >&2; exit 2 ;;
    esac
done

pluginbar_project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
pluginbar_project="$pluginbar_project_root/ACUnreal.uproject"
pluginbar_engine="${ACUE_ENGINE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
pluginbar_builder="$pluginbar_engine/Engine/Build/BatchFiles/Mac/Build.sh"
pluginbar_editor="$pluginbar_engine/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
pluginbar_test_editor="$pluginbar_engine/Engine/Binaries/Mac/UnrealEditor-Cmd"
pluginbar_atlas="$pluginbar_project_root/Plugins/ACEClient/Resources/Icons/PluginIcons.png"

if [[ "$(uname -s)" != Darwin ]]; then
    printf 'This command requires macOS.\n' >&2
    exit 2
fi
for pluginbar_required in "$pluginbar_project" "$pluginbar_builder" "$pluginbar_editor" "$pluginbar_test_editor" "$pluginbar_atlas"; do
    if [[ ! -f "$pluginbar_required" ]]; then
        printf 'Required file is missing: %s\n' "$pluginbar_required" >&2
        exit 2
    fi
done
if ! command -v python3 >/dev/null 2>&1; then
    printf 'python3 is required to verify the renderer test result.\n' >&2
    exit 2
fi

pluginbar_run_dir="$pluginbar_project_root/Saved/Automation/PluginBar/$(date +%Y%m%d-%H%M%S)-$$"
mkdir -p "$pluginbar_run_dir"
printf 'Building the updated client. Logs: %s\n' "$pluginbar_run_dir"
# Run from a normal Terminal session: the restricted agent environment cannot
# create Unreal's trace/shared-memory files or use macOS application services.
"$pluginbar_builder" ACUnrealEditor Mac Development "$pluginbar_project" \
    -WaitMutex -NoHotReloadFromIDE -MaxParallelActions=6 \
    "-Log=$pluginbar_run_dir/Build.log" \
    2>&1 | tee "$pluginbar_run_dir/Build-console.log"

pluginbar_test_filter='ACE.Plugins.IconArtwork+ACE.Plugins.BarPresentation'
if "$pluginbar_full_checks"; then
    pluginbar_test_filter+='+ACE.Plugins.HostAndPanel+ACE.RetailParity.KeyboardBindings'
fi
printf 'Checking painted icons, pinned-window input and launcher backgrounds.\n'
# No NullRHI: this test must exercise the actual texture/Slate rendering path.
# The isolated test profile neither logs into a server nor changes your settings.
"$pluginbar_test_editor" "$pluginbar_project" -game -unattended -RenderOffscreen \
    -nosound -NoSplash -NoP4 -NoLiveCoding \
    "-UserDir=$pluginbar_run_dir/TestUser" \
    "-ExecCmds=Automation RunTests $pluginbar_test_filter" \
    '-TestExit=Automation Test Queue Empty' \
    "-ReportExportPath=$pluginbar_run_dir/Report" \
    "-ABSLOG=$pluginbar_run_dir/Tests.log" \
    2>&1 | tee "$pluginbar_run_dir/Tests-console.log"

python3 - "$pluginbar_run_dir/Tests.log" "$pluginbar_test_filter" <<'PY'
from pathlib import Path
import re
import sys

log = Path(sys.argv[1])
text = log.read_text(errors="replace") if log.exists() else ""
completed = re.findall(
    r"Test Completed\.\s+Result=\{([^}]+)\}[^\n]*Path=\{([^}]+)\}",
    text,
)
failures = {}
for suite in sys.argv[2].split("+"):
    results = [result for result, path in completed if path == suite]
    if not results or any(result != "Success" for result in results):
        failures[suite] = results or ["not executed"]
if failures:
    raise SystemExit(f"Checks did not pass: {failures}. Inspect {log}")
print("Plugin bar, pinned-window and launcher backgrounds checks passed.")
PY

printf 'Validation complete. Logs: %s\n' "$pluginbar_run_dir"
if "$pluginbar_launch"; then
    printf 'Opening AC: Unreal. Log in normally to see the painted plugin bar.\n'
    exec "$pluginbar_editor" "$pluginbar_project" -game -log \
        "-ABSLOG=$pluginbar_run_dir/LiveClient.log"
fi
