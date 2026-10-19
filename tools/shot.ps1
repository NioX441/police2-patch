param([int]$procId, [string]$out)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W2 {
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 public delegate bool EnumProc(IntPtr h, IntPtr l);
 public struct RECT { public int L, T, R, B; }
}
"@
$script:best = $null; $script:area = 0
$cb = [W2+EnumProc]{ param($h, $l)
  $p = 0; [void][W2]::GetWindowThreadProcessId($h, [ref]$p)
  if ($p -eq $procId -and [W2]::IsWindowVisible($h)) {
    $r = New-Object W2+RECT; [void][W2]::GetWindowRect($h, [ref]$r)
    $a = ($r.R - $r.L) * ($r.B - $r.T)
    if ($a -gt $script:area) { $script:area = $a; $script:best = $r; $script:hw = $h }
  }
  $true }
[void][W2]::EnumWindows($cb, [IntPtr]::Zero)
if (-not $script:best) { "no window"; exit }
$r = $script:best
"window rect: $($r.L),$($r.T) - $($r.R),$($r.B)"
[void][W2]::SetForegroundWindow($script:hw); Start-Sleep -Milliseconds 500
$w = $r.R - $r.L; $h = $r.B - $r.T
$bmp = New-Object Drawing.Bitmap $w, $h
$g = [Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object Drawing.Size $w, $h))
$bmp.Save($out)
