"""Long-running instrumented session: spawn game with a Frida script, run a click plan, log messages."""
import frida, sys, time, os, subprocess, json
exe, jsfile, secs, plan, logf = sys.argv[1], sys.argv[2], int(sys.argv[3]), json.load(open(sys.argv[4])), sys.argv[5]
out = open(logf, 'w', encoding='utf-8')
def on(m, d):
    out.write(str(m.get('payload', m)) + '\n'); out.flush()
env = dict(os.environ)
if os.environ.get('P2_LOCALAPPDATA'): env['LOCALAPPDATA'] = os.environ['P2_LOCALAPPDATA']
pid = frida.spawn(exe, cwd=os.path.dirname(exe), env=env)
s = frida.attach(pid); sc = s.create_script(open(jsfile).read()); sc.on('message', on); sc.load(); frida.resume(pid)
here = os.path.dirname(os.path.abspath(__file__))
for step in plan:
    if step[0] == 'wait': time.sleep(step[1])
    elif step[0] == 'click': subprocess.run(['powershell', '-NoProfile', '-File', os.path.join(here, 'click.ps1'), str(step[1]), str(step[2]), '400'])
    elif step[0] == 'shot': subprocess.run(['powershell', '-NoProfile', '-File', os.path.join(here, 'shot.ps1'), str(pid), step[1]])
    elif step[0] == 'drag': subprocess.run(['powershell', '-NoProfile', '-File', os.path.join(here, 'drag2.ps1')] + [str(v) for v in step[1:]])
    elif step[0] == 'keys': subprocess.run(['powershell', '-NoProfile', '-Command', "Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.SendKeys]::SendWait('" + step[1] + "')"])
    elif step[0] == 'rpc': sc.exports_sync.call(step[1]) if False else None
t0 = time.time()
while time.time() - t0 < secs: time.sleep(1)
if os.environ.get('KEEP') != '1':
    try: frida.kill(pid)
    except Exception: pass
out.close()
