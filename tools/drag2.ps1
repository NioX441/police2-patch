param([int]$x1, [int]$y1, [int]$x2, [int]$y2, [int]$times = 1)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class M3 { [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
 [DllImport("user32.dll")] public static extern void mouse_event(int f, int x, int y, int d, int e); }
"@
for ($t = 0; $t -lt $times; $t++) {
  [M3]::SetCursorPos($x1, $y1); Start-Sleep -Milliseconds 80
  [M3]::mouse_event(8, 0, 0, 0, 0)
  for ($i = 1; $i -le 30; $i++) { [M3]::SetCursorPos($x1 + ($x2 - $x1) * $i / 30, $y1 + ($y2 - $y1) * $i / 30); Start-Sleep -Milliseconds 35 }
  [M3]::mouse_event(16, 0, 0, 0, 0); Start-Sleep -Milliseconds 150
}
