param([int]$x, [int]$y, [int]$dx, [int]$steps = 60, [int]$delay = 30)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class M2 { [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
 [DllImport("user32.dll")] public static extern void mouse_event(int f, int x, int y, int d, int e); }
"@
[M2]::SetCursorPos($x, $y); Start-Sleep -Milliseconds 100
[M2]::mouse_event(8, 0, 0, 0, 0)
for ($i = 0; $i -lt $steps; $i++) { [M2]::SetCursorPos($x + [int]($dx * [Math]::Sin($i / 10.0)), $y); Start-Sleep -Milliseconds $delay }
[M2]::mouse_event(16, 0, 0, 0, 0)
