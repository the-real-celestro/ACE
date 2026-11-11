"""Apply project branding and the ACE release version to a staged Mac app, then renew its ad-hoc signature."""
from datetime import datetime
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys

app = Path(sys.argv[1]).resolve()
root = Path(__file__).resolve().parent
header = root.parents[1] / 'Plugins/ACEClient/Source/ACEClient/Public/ACEClientBuild.h'
text = header.read_text()
release_match = re.search(r'ReleaseNumber\s*=\s*(\d+)', text)
version_match = re.search(r'Version\s*=\s*TEXT\("([0-9.]+)"\)', text)
if not release_match or not version_match:
    raise SystemExit(f'Could not read the ACE release version from {header}.')
release = int(release_match.group(1))
version = version_match.group(1)
parts = version.split('.')
if not all(p.isdigit() for p in parts) or not (1 <= len(parts) <= 4):
    raise SystemExit(f'Unexpected ACE version: {version}')
# Apple allows at most three period-separated integers in these keys.
# Fold YYYY.MM.DD.N into YYYY.MMDD.N so the date and release number are kept.
if len(parts) == 4:
    short_version = f'{int(parts[0])}.{int(parts[1]):02d}{int(parts[2]):02d}.{int(parts[3])}'
else:
    short_version = '.'.join(str(int(p)) for p in parts)
# release.yyyymmdd.hhmmss increases for every build and keeps each component in 32-bit range.
now = datetime.now().astimezone()
bundle_version = f'{release}.{now:%Y%m%d}.{now:%H%M%S}'

icon = root / 'Application.icns'
resources = app / 'Contents/Resources'
shutil.copy2(icon, resources / 'Application.icns')
shutil.copy2(icon, resources / 'AppIcon.icns')
info = app / 'Contents/Info.plist'
with info.open('rb') as f:
    data = plistlib.load(f)
data['CFBundleIconFile'] = 'Application.icns'
data.pop('CFBundleIconName', None)
data['CFBundleShortVersionString'] = short_version
data['CFBundleVersion'] = bundle_version
with info.open('wb') as f:
    plistlib.dump(data, f)
subprocess.run([
    'codesign', '--force', '--deep', '--sign', '-',
    '--entitlements', str(root / 'Resources/ACUnreal.Unsandboxed.entitlements'),
    str(app),
], check=True)
subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
print('AC branding and signature verified:', app)
print('CFBundleShortVersionString:', short_version)
print('CFBundleVersion:', bundle_version)
