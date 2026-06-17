"""BLE UART 串口助手入口 —— 参考 demo6/tools/companion/__main__.py。"""
from __future__ import annotations

import asyncio
import logging
import sys
import threading
import time
from pathlib import Path

# 让直接 python __main__.py 也能工作
sys.path.insert(0, str(Path(__file__).parent.parent))

import customtkinter as ctk

from ble_uart.bus import EventBus
from ble_uart.constants import DEVICE_NAME
from ble_uart.core import BLEConnection

logger = logging.getLogger(__name__)

# 主题色
BG = "#1a1a2e"
PANEL = "#16213e"
ACCENT = "#0f9b8e"
TEXT = "#e0e0e0"
MUTED = "#777777"
COLOR_RX = "#9bdeac"
COLOR_TX = "#8ab4f8"
COLOR_ERR = "#e74c3c"
COLOR_INFO = "#f0e68c"

ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("green")


class UartApp(ctk.CTk):
    def __init__(self, bus: EventBus, ble: BLEConnection) -> None:
        super().__init__()
        self.bus = bus
        self.ble = ble

        self.title(f"BLE UART - {DEVICE_NAME}")
        self.geometry("800x600")
        self.minsize(600, 400)
        self.configure(fg_color=BG)

        self._hex_mode = False
        self._build_ui()

        # 注册总线事件（用 after 切回 GUI 线程）
        self.bus.on("connect",    lambda a: self.after(0, self._on_connect, a))
        self.bus.on("disconnect", lambda _: self.after(0, self._on_disconnect))
        self.bus.on("notify",     lambda d: self.after(0, self._on_rx, d))
        self.bus.on("log",        lambda p: self.after(0, self._on_log, p))

        self.protocol("WM_DELETE_WINDOW", self._on_close)

    # ── UI 构建 ──────────────────────────────────────────────────

    def _build_ui(self):
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=1)

        # 顶部连接栏
        top = ctk.CTkFrame(self, fg_color=PANEL, corner_radius=0, height=50)
        top.grid(row=0, column=0, sticky="ew")
        top.grid_columnconfigure(2, weight=1)
        top.grid_propagate(False)

        self._dot = ctk.CTkLabel(top, text="○", text_color=MUTED,
                                  font=ctk.CTkFont(size=18))
        self._dot.grid(row=0, column=0, padx=(16, 4), pady=12)

        self._status_lbl = ctk.CTkLabel(top, text="未连接", text_color=MUTED,
                                         font=ctk.CTkFont(size=13))
        self._status_lbl.grid(row=0, column=1, padx=(0, 12), pady=12)

        self._btn = ctk.CTkButton(top, text="连接", width=80, height=30,
                                   command=self._toggle)
        self._btn.grid(row=0, column=3, padx=12, pady=10)

        # 日志区
        self._log = ctk.CTkTextbox(self, fg_color=PANEL, text_color=TEXT,
                                    font=ctk.CTkFont(family="Consolas", size=13),
                                    corner_radius=8, wrap="word")
        self._log.grid(row=1, column=0, sticky="nsew", padx=8, pady=(4, 0))
        self._log.configure(state="disabled")

        # 底部发送区
        bottom = ctk.CTkFrame(self, fg_color=PANEL, corner_radius=0, height=100)
        bottom.grid(row=2, column=0, sticky="ew", pady=(4, 0))
        bottom.grid_columnconfigure(0, weight=1)
        bottom.grid_propagate(False)

        self._input = ctk.CTkTextbox(bottom, fg_color=BG, text_color=TEXT,
                                      font=ctk.CTkFont(family="Consolas", size=13),
                                      corner_radius=6, height=56, wrap="word")
        self._input.grid(row=0, column=0, sticky="ew", padx=(8, 4), pady=8)
        self._input.bind("<Return>", self._on_enter)
        self._input.bind("<Shift-Return>", lambda e: None)

        btn_frame = ctk.CTkFrame(bottom, fg_color="transparent")
        btn_frame.grid(row=0, column=1, sticky="ns", padx=(0, 8), pady=8)
        btn_frame.grid_rowconfigure(3, weight=1)

        self._send_btn = ctk.CTkButton(btn_frame, text="发送", width=70, height=30,
                                        command=self._send)
        self._send_btn.grid(row=0, column=0, pady=(0, 4))

        self._hex_btn = ctk.CTkButton(btn_frame, text="TXT", width=70, height=30,
                                       fg_color="transparent", border_width=1,
                                       border_color=MUTED, text_color=MUTED,
                                       command=self._toggle_hex)
        self._hex_btn.grid(row=1, column=0, pady=(0, 4))

        ctk.CTkButton(btn_frame, text="清空", width=70, height=30,
                       fg_color="transparent", border_width=1,
                       border_color=MUTED, text_color=MUTED,
                       command=self._clear_log).grid(row=2, column=0)

    # ── 总线回调（GUI 线程）──────────────────────────────────────

    def _on_connect(self, addr: str):
        self._dot.configure(text="●", text_color=ACCENT)
        self._status_lbl.configure(text=f"已连接 {addr[:12]}", text_color=ACCENT)
        self._btn.configure(text="断开", fg_color="#c0392b")
        self._log_append(f"[{_ts()}] 已连接 {addr}\n", COLOR_INFO)

    def _on_disconnect(self):
        self._dot.configure(text="○", text_color=MUTED)
        self._status_lbl.configure(text="已断开", text_color=MUTED)
        self._btn.configure(text="连接", fg_color=ACCENT)
        self._log_append(f"[{_ts()}] 已断开\n", COLOR_INFO)

    def _on_rx(self, data: bytes):
        ts = _ts()
        if self._hex_mode:
            self._log_append(f"[{ts}] RX: {data.hex(' ').upper()}\n", COLOR_RX)
        else:
            text = data.decode("utf-8", errors="replace")
            self._log_append(f"[{ts}] RX: {text}\n", COLOR_RX)

    def _on_log(self, payload):
        level, msg = payload
        color = COLOR_INFO if level == "info" else COLOR_ERR
        self._log_append(f"[{_ts()}] [{level}] {msg}\n", color)

    # ── 日志 ─────────────────────────────────────────────────────

    def _log_append(self, text: str, color: str):
        self._log.configure(state="normal")
        self._log.insert("end", text, ("tag",))
        self._log.tag_config("tag", foreground=color)
        self._log.see("end")
        self._log.configure(state="disabled")

    def _clear_log(self):
        self._log.configure(state="normal")
        self._log.delete("1.0", "end")
        self._log.configure(state="disabled")

    def _toggle_hex(self):
        self._hex_mode = not self._hex_mode
        if self._hex_mode:
            self._hex_btn.configure(text="HEX", fg_color=ACCENT,
                                     text_color=TEXT, border_width=0)
        else:
            self._hex_btn.configure(text="TXT", fg_color="transparent",
                                     border_width=1, text_color=MUTED)

    # ── 发送 ─────────────────────────────────────────────────────

    def _send(self):
        raw = self._input.get("1.0", "end-1c").strip()
        if not raw:
            return
        if self._hex_mode:
            s = raw.replace(" ", "")
            if len(s) % 2:
                self._log_append(f"[{_ts()}] 错误: HEX 长度需为偶数\n", COLOR_ERR)
                return
            try:
                data = bytes.fromhex(s)
            except ValueError:
                self._log_append(f"[{_ts()}] 错误: HEX 格式无效\n", COLOR_ERR)
                return
        else:
            data = raw.encode("utf-8")
        ts = _ts()
        preview = data.hex(" ").upper() if self._hex_mode else raw
        self._log_append(f"[{ts}] TX: {preview}\n", COLOR_TX)
        self._input.delete("1.0", "end")

        async def _write():
            try:
                await self.ble.write(data)
            except Exception as e:
                self.after(0, self._log_append, f"[{_ts()}] 发送失败: {e}\n", COLOR_ERR)
        asyncio.run_coroutine_threadsafe(_write(), self.ble._loop)

    def _on_enter(self, event):
        if not (event.state & 0x1):
            self._send()
            return "break"

    # ── 连接切换 ────────────────────────────────────────────────

    def _toggle(self):
        if self.ble.is_connected:
            self.ble.stop()
        else:
            self.ble._stopped.clear()
            asyncio.run_coroutine_threadsafe(self.ble.run(), self.ble._loop)

    # ── 退出 ─────────────────────────────────────────────────────

    def _on_close(self):
        self.ble.stop()
        self.destroy()


def _ts() -> str:
    return time.strftime("%H:%M:%S")


class AsyncRunner:
    """管理 asyncio 事件循环线程。"""

    def __init__(self):
        self.loop = asyncio.new_event_loop()
        self._thread = None

    def start(self):
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        asyncio.set_event_loop(self.loop)
        self.loop.run_forever()

    def stop(self):
        self.loop.call_soon_threadsafe(self.loop.stop)
        if self._thread:
            self._thread.join(timeout=3.0)


def main():
    logging.basicConfig(level=logging.WARNING)
    bus = EventBus()
    ble = BLEConnection(bus)
    runner = AsyncRunner()
    runner.start()
    bus.set_loop(runner.loop)
    ble._loop = runner.loop

    app = UartApp(bus, ble)
    try:
        app.mainloop()
    finally:
        ble.stop()
        runner.stop()


if __name__ == "__main__":
    main()
