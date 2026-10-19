"""Attach a Frida script to the running police2 process for N seconds."""
import frida, sys, time
name, js, secs = sys.argv[1], open(sys.argv[2]).read(), int(sys.argv[3])
s = frida.attach(name); sc = s.create_script(js)
sc.on('message', lambda m, d: print(m.get('payload', m), flush=True)); sc.load()
time.sleep(secs); s.detach()
