# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sends both of the Windows message-only windows things nothing friendly would send, and
# checks that neither process minds.
#
# This is here instead of a fuzz target, and the reason is worth stating. The boundary is
# real -- any process in the session can send WM_COPYDATA to DragonPerch.Control or to
# DragonPerch.Shell -- but there is nothing on the other side for a fuzzer to explore: one
# side compares against five fixed strings, the other splits on spaces and expects four
# fields. Coverage saturates in the first hundred executions and then finds nothing for
# ever.
#
# What is worth probing is the framing rather than the text: how large a payload is
# accepted, what a non-UTF-8 one does, whether an exception can cross back into native
# code, and how much of a stranger's bytes end up in somebody's log file. A fuzzer sending
# well-formed COPYDATASTRUCTs of its own length never asks any of those. This does.
#
# Usage: start the daemon (and optionally the shell), then
#
#     pwsh -File tools/hostile_ipc.ps1 -Daemon <directory holding dragonperch.exe>

param([string]$Daemon = "build/windows-x64/src/win/Release")

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class HostileIpc {
  [StructLayout(LayoutKind.Sequential)]
  public struct CDS { public IntPtr dwData; public uint cbData; public IntPtr lpData; }

  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string n);

  [DllImport("user32.dll")]
  public static extern IntPtr SendMessageTimeout(IntPtr h, uint m, IntPtr w, IntPtr l,
                                                 uint f, uint t, out IntPtr r);

  public static IntPtr Find(string cls) {
    return FindWindowEx(new IntPtr(-3), IntPtr.Zero, cls, null);
  }

  public static bool Send(IntPtr target, byte[] payload, uint declaredLength) {
    IntPtr buf = Marshal.AllocHGlobal(Math.Max(payload.Length, 1));
    IntPtr packed = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(CDS)));
    try {
      if (payload.Length > 0) Marshal.Copy(payload, 0, buf, payload.Length);
      CDS d = new CDS();
      d.cbData = declaredLength;
      d.lpData = buf;
      Marshal.StructureToPtr(d, packed, false);
      IntPtr res;
      return SendMessageTimeout(target, 0x004A, IntPtr.Zero, packed, 0x0002, 5000, out res)
             != IntPtr.Zero;   // 0x004A = WM_COPYDATA, 0x0002 = SMTO_ABORTIFHUNG
    } finally { Marshal.FreeHGlobal(packed); Marshal.FreeHGlobal(buf); }
  }
}
"@ -ErrorAction SilentlyContinue

function Alive([string]$name) {
    return [bool](Get-Process -Name $name -ErrorAction SilentlyContinue)
}

function Hammer([string]$class, [string]$label) {
    $window = [HostileIpc]::Find($class)
    if ($window -eq [IntPtr]::Zero) { Write-Host "  $label : not running, skipped"; return }

    $cases = @(
        @('empty',                [byte[]]@()),
        @('one NUL',              [byte[]]@(0)),
        @('almost a command',     [byte[]][System.Text.Encoding]::UTF8.GetBytes('quit-but-not-quite')),
        @('invalid UTF-8',        [byte[]]@(0xC3, 0x28, 0xFF, 0xFE, 0x80)),
        @('control characters',   [byte[]]@(1,2,3,7,8,10,13,27,0,1,2)),
        @('4 KB',                 [byte[]][System.Text.Encoding]::ASCII.GetBytes(('A' * 4096))),
        @('1 MB',                 [byte[]][System.Text.Encoding]::ASCII.GetBytes(('A' * 1048576))),
        @('16 MB of zeroes',      [byte[]]::new(16777216)),
        @('menu, huge numbers',   [byte[]][System.Text.Encoding]::UTF8.GetBytes('menu 99999999999 -99999999999 paused')),
        @('menu, too few fields', [byte[]][System.Text.Encoding]::UTF8.GetBytes('menu 1')),
        @('menu, 2000 fields',    [byte[]][System.Text.Encoding]::UTF8.GetBytes('menu ' + ('1 ' * 2000)))
    )

    foreach ($case in $cases) {
        $bytes = [byte[]]$case[1]
        [void][HostileIpc]::Send($window, $bytes, [uint32]$bytes.Length)
        Write-Host ("    {0,-22} {1} byte(s)" -f $case[0], $bytes.Length)
        Start-Sleep -Milliseconds 60
    }
}

$log = Join-Path $Daemon "dragonperch.log"
$before = if (Test-Path $log) { (Get-Item $log).Length } else { 0 }

Write-Host "before:  daemon=$(Alive 'dragonperch')  shell=$(Alive 'DragonPerch.Shell')"
Write-Host "--- DragonPerch.Control"; Hammer "DragonPerch.Control" "daemon"
Write-Host "--- DragonPerch.Shell";   Hammer "DragonPerch.Shell" "shell"
Start-Sleep -Seconds 2
Write-Host "after:   daemon=$(Alive 'dragonperch')  shell=$(Alive 'DragonPerch.Shell')"

# The number that matters as much as the survival: about 17 MB went in, and the log is
# capped at sixty printable characters per rejected command. Anything close to the payload
# size here means a stranger can fill somebody's disk by talking to the tray.
if (Test-Path $log) {
    $grew = (Get-Item $log).Length - $before
    Write-Host ("log grew by {0:N0} bytes against roughly 17 MB sent" -f $grew)
}
