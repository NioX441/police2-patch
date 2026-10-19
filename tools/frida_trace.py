"""Spawn police2.exe under Frida and trace Asphyre DX7 init steps (after UPX unpacking)."""
import frida, sys, time
exe = sys.argv[1]; secs = int(sys.argv[2]) if len(sys.argv) > 2 else 15
extra = open(sys.argv[3]).read() if len(sys.argv) > 3 else ''
JS = r"""
var hooked = false;
function hookAll() {
  if (hooked) return; hooked = true;
  var steps = {0x593a4c:'CreateDDraw',0x593aa0:'QueryD3D7',0x593af8:'SetCoopLevel',0x593b28:'CreatePrimary',
               0x593b9c:'CreateBackBuffer',0x593c9c:'CreateClipper',0x593d24:'CreateD3DDevice',0x593d78:'InitDevice',
               0x5f3de4:'OnDeviceInitFailed',0x5f3f4c:'OnDeviceCreate'};
  for (var a in steps) (function(a, n){
    Interceptor.attach(ptr(a), {
      onEnter: function(args){ this.edx = this.context.edx; send(n + ' enter edx=' + this.context.edx); },
      onLeave: function(r){ send(n + ' -> ' + (r.toInt32() & 0xff)); }
    });
  })(parseInt(a), steps[a]);
  EXTRA
}
if (EARLY) hookAll();
var t = setInterval(function(){
  var m = Process.findModuleByName('ddraw.dll'); if (!m) return; clearInterval(t);
  Interceptor.attach(m.getExportByName('DirectDrawCreateEx'), { onEnter: function(){ hookAll(); }, onLeave: function(r){ send('DirectDrawCreateEx hr=' + r); } });
}, 1);
""".replace('EXTRA', extra).replace('EARLY', 'true' if 'testgame' in exe else 'false')
pid = frida.spawn(exe, cwd=__import__('os').path.dirname(exe))
s = frida.attach(pid)
sc = s.create_script(JS)
sc.on('message', lambda m, d: print(m.get('payload', m), flush=True))
sc.load(); frida.resume(pid)
time.sleep(secs)
try: frida.kill(pid)
except Exception: pass
