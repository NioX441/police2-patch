"""Run police2.exe (test copy) under Frida with a hook script; keep it alive for N seconds, optional screenshot."""
import frida, sys, time, os, subprocess
exe, js, secs = sys.argv[1], open(sys.argv[2]).read(), int(sys.argv[3])
pid = frida.spawn(exe, cwd=os.path.dirname(exe))
s = frida.attach(pid); sc = s.create_script(js)
sc.on('message', lambda m, d: print(m.get('payload', m), flush=True))
sc.load(); frida.resume(pid)
time.sleep(secs)
if len(sys.argv) > 4:
    subprocess.run(['powershell', '-NoProfile', '-File', sys.argv[4], str(pid), sys.argv[5]])
if len(sys.argv) <= 6:
    try: frida.kill(pid)
    except Exception: pass
