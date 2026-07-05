param(
    [ValidateSet("left", "right")]
    [string]$Button = "left",

    [int]$IntervalMs = 100,
    [int]$HoldMs = 20,
    [int]$JitterMs = 0,

    [switch]$StartActive,
    [switch]$SelfTest
)

if ($IntervalMs -lt 1) {
    throw "IntervalMs must be at least 1."
}
if ($HoldMs -lt 0) {
    throw "HoldMs cannot be negative."
}
if ($JitterMs -lt 0) {
    throw "JitterMs cannot be negative."
}

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Threading;

namespace NoArduinoAutoClicker
{
    public static class Native
    {
        private const uint INPUT_MOUSE = 0;
        private const uint MOUSEEVENTF_LEFTDOWN = 0x0002;
        private const uint MOUSEEVENTF_LEFTUP = 0x0004;
        private const uint MOUSEEVENTF_RIGHTDOWN = 0x0008;
        private const uint MOUSEEVENTF_RIGHTUP = 0x0010;

        [StructLayout(LayoutKind.Sequential)]
        private struct MOUSEINPUT
        {
            public int dx;
            public int dy;
            public uint mouseData;
            public uint dwFlags;
            public uint time;
            public UIntPtr dwExtraInfo;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct INPUT
        {
            public uint type;
            public MOUSEINPUT mi;
        }

        [DllImport("user32.dll", SetLastError = true)]
        private static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);

        [DllImport("user32.dll")]
        public static extern short GetAsyncKeyState(int vKey);

        private static void SendMouse(uint flags)
        {
            INPUT[] inputs = new INPUT[1];
            inputs[0].type = INPUT_MOUSE;
            inputs[0].mi.dwFlags = flags;

            uint sent = SendInput(1, inputs, Marshal.SizeOf(typeof(INPUT)));
            if (sent != 1)
            {
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            }
        }

        public static void Click(string button, int holdMs)
        {
            uint down = button == "right" ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
            uint up = button == "right" ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;

            SendMouse(down);
            if (holdMs > 0)
            {
                Thread.Sleep(holdMs);
            }
            SendMouse(up);
        }
    }
}
"@

if ($SelfTest) {
    Write-Host "Self-test OK: Win32 input helpers compiled."
    exit 0
}

$VK_F8 = 0x77
$VK_F9 = 0x78
$VK_F10 = 0x79

$running = [bool]$StartActive
$lastClick = [Environment]::TickCount64
$nextExtraDelay = 0
$previousF8 = $false
$previousF9 = $false
$previousF10 = $false
$random = [Random]::new()

function Test-KeyDown([int]$vk) {
    return (([NoArduinoAutoClicker.Native]::GetAsyncKeyState($vk) -band 0x8000) -ne 0)
}

function Write-Status {
    if ($running) {
        Write-Host "Status: RUNNING"
    } else {
        Write-Host "Status: paused"
    }
}

Write-Host "No-Arduino AutoClicker"
Write-Host "----------------------"
Write-Host "Button: $Button"
Write-Host "Interval: $IntervalMs ms"
Write-Host "Hold: $HoldMs ms"
Write-Host "Jitter: $JitterMs ms"
Write-Host ""
Write-Host "F8  = toggle auto-click"
Write-Host "F9  = single click"
Write-Host "F10 = quit"
Write-Host ""
Write-Status

while ($true) {
    $f8 = Test-KeyDown $VK_F8
    $f9 = Test-KeyDown $VK_F9
    $f10 = Test-KeyDown $VK_F10

    if ($f8 -and -not $previousF8) {
        $running = -not $running
        $lastClick = [Environment]::TickCount64
        if ($JitterMs -gt 0) {
            $nextExtraDelay = $random.Next(0, $JitterMs + 1)
        }
        Write-Status
    }

    if ($f9 -and -not $previousF9) {
        [NoArduinoAutoClicker.Native]::Click($Button, $HoldMs)
        Write-Host "Single click"
    }

    if ($f10 -and -not $previousF10) {
        Write-Host "Quitting"
        break
    }

    $previousF8 = $f8
    $previousF9 = $f9
    $previousF10 = $f10

    if ($running) {
        $now = [Environment]::TickCount64
        if (($now - $lastClick) -ge ($IntervalMs + $nextExtraDelay)) {
            [NoArduinoAutoClicker.Native]::Click($Button, $HoldMs)
            $lastClick = [Environment]::TickCount64
            if ($JitterMs -gt 0) {
                $nextExtraDelay = $random.Next(0, $JitterMs + 1)
            } else {
                $nextExtraDelay = 0
            }
        }
    }

    Start-Sleep -Milliseconds 10
}
