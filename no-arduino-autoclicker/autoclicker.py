#!/usr/bin/env python3
"""Small Windows auto-clicker with global hotkeys and no third-party deps."""

from __future__ import annotations

import argparse
import ctypes
import random
import signal
import sys
import threading
import time
from ctypes import wintypes


if sys.platform != "win32":
    raise SystemExit("This tool only runs on Windows.")


user32 = ctypes.WinDLL("user32", use_last_error=True)

INPUT_MOUSE = 0
MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004
MOUSEEVENTF_RIGHTDOWN = 0x0008
MOUSEEVENTF_RIGHTUP = 0x0010

WM_HOTKEY = 0x0312
MOD_NOREPEAT = 0x4000

HOTKEY_TOGGLE = 1
HOTKEY_SINGLE_CLICK = 2
HOTKEY_QUIT = 3

VK_F8 = 0x77
VK_F9 = 0x78
VK_F10 = 0x79

ULONG_PTR = wintypes.WPARAM


class MouseInput(ctypes.Structure):
    _fields_ = (
        ("dx", wintypes.LONG),
        ("dy", wintypes.LONG),
        ("mouseData", wintypes.DWORD),
        ("dwFlags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", ULONG_PTR),
    )


class InputUnion(ctypes.Union):
    _fields_ = (("mi", MouseInput),)


class Input(ctypes.Structure):
    _fields_ = (("type", wintypes.DWORD), ("union", InputUnion))


def _send_mouse(flags: int) -> None:
    mouse_input = MouseInput(0, 0, 0, flags, 0, 0)
    event = Input(INPUT_MOUSE, InputUnion(mi=mouse_input))
    sent = user32.SendInput(1, ctypes.byref(event), ctypes.sizeof(event))
    if sent != 1:
        raise ctypes.WinError(ctypes.get_last_error())


def click(button: str, hold_ms: int) -> None:
    if button == "left":
        down, up = MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP
    else:
        down, up = MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP

    _send_mouse(down)
    if hold_ms > 0:
        time.sleep(hold_ms / 1000.0)
    _send_mouse(up)


class AutoClicker:
    def __init__(self, button: str, interval_ms: int, hold_ms: int, jitter_ms: int) -> None:
        self.button = button
        self.interval_ms = interval_ms
        self.hold_ms = hold_ms
        self.jitter_ms = jitter_ms
        self._active = False
        self._stop = threading.Event()
        self._lock = threading.Lock()
        self._thread = threading.Thread(target=self._run, name="AutoClicker", daemon=True)

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=1.0)

    def toggle(self) -> bool:
        with self._lock:
            self._active = not self._active
            return self._active

    def set_active(self, active: bool) -> None:
        with self._lock:
            self._active = active

    def is_active(self) -> bool:
        with self._lock:
            return self._active

    def single_click(self) -> None:
        click(self.button, self.hold_ms)

    def _run(self) -> None:
        while not self._stop.is_set():
            if not self.is_active():
                self._stop.wait(0.05)
                continue

            click(self.button, self.hold_ms)
            delay_ms = self.interval_ms
            if self.jitter_ms > 0:
                delay_ms += random.randint(0, self.jitter_ms)
            self._stop.wait(max(delay_ms, 1) / 1000.0)


def register_hotkey(hotkey_id: int, vk: int) -> None:
    if not user32.RegisterHotKey(None, hotkey_id, MOD_NOREPEAT, vk):
        raise ctypes.WinError(ctypes.get_last_error())


def unregister_hotkeys() -> None:
    for hotkey_id in (HOTKEY_TOGGLE, HOTKEY_SINGLE_CLICK, HOTKEY_QUIT):
        user32.UnregisterHotKey(None, hotkey_id)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="No-Arduino desktop auto-clicker.")
    parser.add_argument("--button", choices=("left", "right"), default="left")
    parser.add_argument("--interval-ms", type=int, default=100, help="Delay between clicks.")
    parser.add_argument("--hold-ms", type=int, default=20, help="Mouse button hold duration.")
    parser.add_argument("--jitter-ms", type=int, default=0, help="Optional extra random delay.")
    parser.add_argument("--start-active", action="store_true", help="Begin clicking immediately.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.interval_ms < 1:
        raise SystemExit("--interval-ms must be at least 1")
    if args.hold_ms < 0:
        raise SystemExit("--hold-ms cannot be negative")
    if args.jitter_ms < 0:
        raise SystemExit("--jitter-ms cannot be negative")

    clicker = AutoClicker(args.button, args.interval_ms, args.hold_ms, args.jitter_ms)
    clicker.set_active(args.start_active)
    clicker.start()

    def shutdown(*_unused: object) -> None:
        clicker.set_active(False)
        clicker.stop()
        unregister_hotkeys()
        user32.PostQuitMessage(0)

    signal.signal(signal.SIGINT, shutdown)

    try:
        register_hotkey(HOTKEY_TOGGLE, VK_F8)
        register_hotkey(HOTKEY_SINGLE_CLICK, VK_F9)
        register_hotkey(HOTKEY_QUIT, VK_F10)
    except OSError as exc:
        clicker.stop()
        unregister_hotkeys()
        print(f"Could not register F8/F9/F10 hotkeys: {exc}", file=sys.stderr)
        print("Close apps that already use those keys, then run again.", file=sys.stderr)
        return 1

    print("No-Arduino AutoClicker")
    print("----------------------")
    print(f"Button: {args.button}")
    print(f"Interval: {args.interval_ms} ms")
    print(f"Hold: {args.hold_ms} ms")
    print(f"Jitter: {args.jitter_ms} ms")
    print("")
    print("F8  = toggle auto-click")
    print("F9  = single click")
    print("F10 = quit")
    print("")
    print("Status:", "RUNNING" if clicker.is_active() else "paused")

    msg = wintypes.MSG()
    try:
        while user32.GetMessageW(ctypes.byref(msg), None, 0, 0) != 0:
            if msg.message == WM_HOTKEY:
                hotkey_id = int(msg.wParam)
                if hotkey_id == HOTKEY_TOGGLE:
                    active = clicker.toggle()
                    print("Status:", "RUNNING" if active else "paused")
                elif hotkey_id == HOTKEY_SINGLE_CLICK:
                    clicker.single_click()
                    print("Single click")
                elif hotkey_id == HOTKEY_QUIT:
                    print("Quitting")
                    break
            user32.TranslateMessage(ctypes.byref(msg))
            user32.DispatchMessageW(ctypes.byref(msg))
    finally:
        clicker.set_active(False)
        clicker.stop()
        unregister_hotkeys()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
