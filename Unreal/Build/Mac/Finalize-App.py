"""Apply project branding to a staged Mac app and renew its ad-hoc signature."""
from pathlib import Path
import plistlib,shutil,subprocess,sys
app=Path(sys.argv[1]).resolve()
root=Path(__file__).resolve().parent
icon=root/'Application.icns'
resources=app/'Contents/Resources'
shutil.copy2(icon,resources/'Application.icns')
shutil.copy2(icon,resources/'AppIcon.icns')
info=app/'Contents/Info.plist'
with info.open('rb') as f:data=plistlib.load(f)
data['CFBundleIconFile']='Application.icns'
data.pop('CFBundleIconName',None)
with info.open('wb') as f:plistlib.dump(data,f)
subprocess.run(['codesign','--force','--deep','--sign','-','--entitlements',str(root/'Resources/ACUnreal.Unsandboxed.entitlements'),str(app)],check=True)
subprocess.run(['codesign','--verify','--deep','--strict',str(app)],check=True)
print('AC branding and signature verified:',app)
