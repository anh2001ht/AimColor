#!/usr/bin/env python3
"""Local click target used to verify the auto-clicker safely."""

from __future__ import annotations

import ctypes
import tkinter as tk
from tkinter import ttk


TARGET_COLOR = "#EB69FE"  # RGB(235, 105, 254)
TARGET_RGB = "235, 105, 254"
TARGET_SIZE = 260

user32 = ctypes.WinDLL("user32", use_last_error=True)


def set_cursor_pos(x: int, y: int) -> None:
    if not user32.SetCursorPos(int(x), int(y)):
        raise ctypes.WinError(ctypes.get_last_error())


class ClickTester(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("No-Arduino Click Tester")
        self.geometry("560x520")
        self.minsize(460, 430)
        self.configure(bg="#111318")

        self.total_clicks = 0
        self.target_clicks = 0
        self.missed_clicks = 0
        self.auto_pull = False

        self._build_ui()
        self.bind("<F6>", lambda _event: self._pull_cursor_to_target())
        self.bind("<F7>", lambda _event: self._toggle_auto_pull())
        self.after(50, self._auto_pull_loop)

    def _build_ui(self) -> None:
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure("TLabel", background="#111318", foreground="#EEF1F5")
        style.configure("Header.TLabel", font=("Segoe UI", 16, "bold"))
        style.configure("Stat.TLabel", font=("Segoe UI", 12))
        style.configure("Hint.TLabel", foreground="#B8C0CC")
        style.configure("TButton", font=("Segoe UI", 10))
        style.configure("Root.TFrame", background="#111318")

        frame = ttk.Frame(self, padding=18, style="Root.TFrame")
        frame.pack(fill="both", expand=True)

        ttk.Label(frame, text="Click tester", style="Header.TLabel").pack(anchor="w")
        ttk.Label(
            frame,
            text=f"Target color: {TARGET_COLOR} / RGB({TARGET_RGB})",
            style="Hint.TLabel",
        ).pack(anchor="w", pady=(4, 14))

        self.canvas = tk.Canvas(
            frame,
            width=TARGET_SIZE,
            height=TARGET_SIZE,
            bg="#111318",
            highlightthickness=0,
        )
        self.canvas.pack(pady=(0, 14))

        self.target_id = self.canvas.create_rectangle(
            8,
            8,
            TARGET_SIZE - 8,
            TARGET_SIZE - 8,
            fill=TARGET_COLOR,
            outline="#FFFFFF",
            width=3,
        )
        self.canvas.create_text(
            TARGET_SIZE // 2,
            TARGET_SIZE // 2,
            text="PUT CURSOR HERE\nTHEN PRESS F8",
            fill="#111318",
            font=("Segoe UI", 18, "bold"),
            justify="center",
        )

        self.canvas.bind("<Button-1>", self._target_clicked)
        self.bind("<Button-1>", self._window_clicked)

        self.stats = ttk.Label(frame, text="", style="Stat.TLabel")
        self.stats.pack(anchor="w", pady=(2, 12))
        self._render_stats()

        ttk.Label(
            frame,
            text="F6 pulls cursor once. F7 toggles local auto-pull. F8 toggles auto-clicker. F9 sends one click. F10 closes the auto-clicker window.",
            style="Hint.TLabel",
            wraplength=500,
        ).pack(anchor="w", pady=(0, 10))

        buttons = ttk.Frame(frame, style="Root.TFrame")
        buttons.pack(anchor="w")

        ttk.Button(buttons, text="Reset counter", command=self._reset).pack(side="left")
        ttk.Button(buttons, text="Keep on top", command=self._toggle_topmost).pack(
            side="left",
            padx=(8, 0),
        )
        ttk.Button(buttons, text="Pull cursor", command=self._pull_cursor_to_target).pack(
            side="left",
            padx=(8, 0),
        )
        self.auto_pull_button = ttk.Button(
            buttons,
            text="Auto-pull: off",
            command=self._toggle_auto_pull,
        )
        self.auto_pull_button.pack(side="left", padx=(8, 0))

    def _target_clicked(self, _event: tk.Event) -> str:
        self.total_clicks += 1
        self.target_clicks += 1
        self._flash("#FFFFFF")
        self._render_stats()
        return "break"

    def _window_clicked(self, _event: tk.Event) -> None:
        self.total_clicks += 1
        self.missed_clicks += 1
        self._render_stats()

    def _reset(self) -> None:
        self.total_clicks = 0
        self.target_clicks = 0
        self.missed_clicks = 0
        self._render_stats()

    def _toggle_topmost(self) -> None:
        current = bool(self.attributes("-topmost"))
        self.attributes("-topmost", not current)

    def _pull_cursor_to_target(self) -> None:
        self.update_idletasks()
        x = self.canvas.winfo_rootx() + TARGET_SIZE // 2
        y = self.canvas.winfo_rooty() + TARGET_SIZE // 2
        set_cursor_pos(x, y)

    def _toggle_auto_pull(self) -> None:
        self.auto_pull = not self.auto_pull
        self.auto_pull_button.configure(text=f"Auto-pull: {'on' if self.auto_pull else 'off'}")

    def _auto_pull_loop(self) -> None:
        if self.auto_pull:
            self._pull_cursor_to_target()
        self.after(50, self._auto_pull_loop)

    def _flash(self, color: str) -> None:
        self.canvas.itemconfigure(self.target_id, outline=color)
        self.after(80, lambda: self.canvas.itemconfigure(self.target_id, outline="#FFFFFF"))

    def _render_stats(self) -> None:
        self.stats.configure(
            text=(
                f"Clicks on target: {self.target_clicks}    "
                f"Missed/window clicks: {self.missed_clicks}    "
                f"Total: {self.total_clicks}"
            )
        )


if __name__ == "__main__":
    ClickTester().mainloop()
