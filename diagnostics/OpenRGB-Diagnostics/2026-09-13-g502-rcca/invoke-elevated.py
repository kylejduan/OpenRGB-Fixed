import base64, subprocess, sys
from pathlib import PureWindowsPath
name = sys.argv[1]
if name != PureWindowsPath(name).name or not name.endswith(".ps1"):
    raise SystemExit("Expected a script filename")
args = ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", '"C:\\OpenRGB-Diagnostics\\2026-09-13-g502-rcca\\' + name + '"'] + sys.argv[2:]
def quote(value):
    return "'" + value.replace("'", "''") + "'"
cmd = "$p=Start-Process powershell.exe -Verb RunAs -WindowStyle Hidden -ArgumentList @(" + ",".join(quote(a) for a in args) + ") -PassThru; $handle=$p.Handle; if(-not $p.WaitForExit(45000)){throw ('Diagnostic deadline exceeded: '+$p.Id)}; [pscustomobject]@{Pid=$p.Id;ExitCode=$p.ExitCode}|ConvertTo-Json; exit $p.ExitCode"
proc = subprocess.run(["/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe", "-NoProfile", "-EncodedCommand", base64.b64encode(cmd.encode("utf-16le")).decode()], capture_output=True, text=True)
print(proc.stdout)
if proc.returncode: print(proc.stderr)
raise SystemExit(proc.returncode)
