param([int]$x, [int]$y, [int]$delayMs = 300)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class M { [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
 [DllImport("user32.dll")] public static extern void mouse_event(int f, int x, int y, int d, int e); }
"@
[M]::SetCursorPos($x, $y); Start-Sleep -Milliseconds 150
[M]::mouse_event(2, 0, 0, 0, 0); Start-Sleep -Milliseconds 80; [M]::mouse_event(4, 0, 0, 0, 0)
Start-Sleep -Milliseconds $delayMs
