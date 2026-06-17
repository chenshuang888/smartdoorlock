"""BLE UART 串口助手 — 自动密钥配对"""
import asyncio
import logging
import os
import sys
import threading
import time

import customtkinter as ctk
from bleak import BleakScanner, BleakClient

logger = logging.getLogger(__name__)

DEVICE_NAME = "ESP32-SmartLock"
TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
KEY_FILE = "ble_key.txt"

ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("green")

BG = "#1a1a2e"
PANEL = "#16213e"
ACCENT = "#0f9b8e"
COLOR_RX = "#9bdeac"
COLOR_TX = "#8ab4f8"
COLOR_ERR = "#e74c3c"
COLOR_INFO = "#f0e68c"
COLOR_PWD = "#e8a87c"
MUTED = "#777777"


class App(ctk.CTk):
    def __init__(self):
        super().__init__()
        self.title("BLE 串口助手 - Smart Door Lock")
        self.geometry("800x600")
        self.minsize(600, 400)
        self.configure(fg_color=BG)

        self._client = None
        self._hex = False
        self._key = self._load_key()
        self._authenticated = False
        self._auth_pending = False      # 有 after(_send_auth) 待执行
        self._loop = asyncio.new_event_loop()
        self._running = True

        self._build_ui()
        self.protocol("WM_DELETE_WINDOW", self._on_close)

        t = threading.Thread(target=self._run_loop, daemon=True)
        t.start()

    # ── 密钥文件 ──────────────────────────────────────────────────────
    def _load_key(self):
        if os.path.exists(KEY_FILE):
            with open(KEY_FILE) as f:
                return f.read().strip()
        return None

    def _save_key(self, key):
        self._key = key
        with open(KEY_FILE, "w") as f:
            f.write(key.strip())

    def _delete_key(self):
        self._key = None
        if os.path.exists(KEY_FILE):
            os.remove(KEY_FILE)

    # ── UI ──────────────────────────────────────────────────────────
    def _build_ui(self):
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=1)

        top = ctk.CTkFrame(self, fg_color=PANEL, corner_radius=0, height=50)
        top.grid(row=0, column=0, sticky="ew")
        top.grid_columnconfigure(2, weight=1)
        top.grid_propagate(False)

        self._dot = ctk.CTkLabel(top, text="○", text_color=MUTED,
                                  font=ctk.CTkFont(size=18))
        self._dot.grid(row=0, column=0, padx=(16, 4), pady=12)
        self._status = ctk.CTkLabel(top, text="未连接", text_color=MUTED,
                                     font=ctk.CTkFont(size=13))
        self._status.grid(row=0, column=1, padx=(0, 12), pady=12)
        self._btn = ctk.CTkButton(top, text="连接", width=80, height=30,
                                   command=self._toggle)
        self._btn.grid(row=0, column=3, padx=(4, 12), pady=10)

        self._log = ctk.CTkTextbox(self, fg_color=PANEL, text_color="#e0e0e0",
                                    font=ctk.CTkFont(family="Consolas", size=13),
                                    corner_radius=8, wrap="word")
        self._log.grid(row=1, column=0, sticky="nsew", padx=8, pady=(4, 0))
        self._log.configure(state="disabled")

        bot = ctk.CTkFrame(self, fg_color=PANEL, corner_radius=0, height=200)
        bot.grid(row=2, column=0, sticky="ew", pady=(4, 0))
        bot.grid_columnconfigure(0, weight=1)
        bot.grid_propagate(False)

        # — 第一行：输入 + 发送控制 —
        top_row = ctk.CTkFrame(bot, fg_color="transparent")
        top_row.grid(row=0, column=0, sticky="nsew")
        top_row.grid_columnconfigure(0, weight=1)

        self._input = ctk.CTkTextbox(top_row, fg_color=BG, text_color="#e0e0e0",
                                      font=ctk.CTkFont(family="Consolas", size=13),
                                      corner_radius=6, height=56, wrap="word")
        self._input.grid(row=0, column=0, sticky="ew", padx=(8, 4), pady=(8, 4))
        self._input.bind("<Return>", self._on_enter)

        bf = ctk.CTkFrame(top_row, fg_color="transparent")
        bf.grid(row=0, column=1, sticky="ns", padx=(0, 8), pady=8)
        bf.grid_rowconfigure(6, weight=1)
        ctk.CTkButton(bf, text="发送", width=70, height=28,
                       command=self._send).grid(row=0, column=0, pady=(0, 2))
        self._hex_btn = ctk.CTkButton(bf, text="TXT", width=70, height=28,
                                       fg_color="transparent", border_width=1,
                                       border_color=MUTED, text_color=MUTED,
                                       command=self._toggle_hex)
        self._hex_btn.grid(row=1, column=0, pady=(0, 2))
        ctk.CTkButton(bf, text="清空", width=70, height=28,
                       fg_color="transparent", border_width=1,
                       border_color=MUTED, text_color=MUTED,
                       command=self._clear).grid(row=2, column=0, pady=(0, 2))
        ctk.CTkButton(bf, text="解绑", width=70, height=28,
                       fg_color="transparent", border_width=1,
                       border_color="#c0392b", text_color="#c0392b",
                       command=self._unpair).grid(row=3, column=0, pady=(0, 2))

        # — 第二行：密码操作 —
        pwd_row = ctk.CTkFrame(bot, fg_color="transparent", height=40)
        pwd_row.grid(row=1, column=0, sticky="ew", padx=8, pady=(0, 6))
        pwd_row.grid_columnconfigure(1, weight=1)
        pwd_row.grid_propagate(False)

        ctk.CTkLabel(pwd_row, text="密码:", text_color=MUTED,
                      font=ctk.CTkFont(size=13)).grid(row=0, column=0, padx=(0, 4))
        self._pwd_entry = ctk.CTkEntry(pwd_row, placeholder_text="6位数字",
                                        width=140, height=28)
        self._pwd_entry.grid(row=0, column=1, sticky="w", padx=(0, 8))
        self._pwd_entry.bind("<Return>", lambda e: self._set_pwd())
        ctk.CTkButton(pwd_row, text="查密", width=60, height=28,
                       command=self._query_pwd).grid(row=0, column=2, padx=(0, 4))
        ctk.CTkButton(pwd_row, text="设密", width=60, height=28,
                       fg_color="transparent", border_width=1, border_color=ACCENT,
                       text_color=ACCENT,
                       command=self._set_pwd).grid(row=0, column=3, padx=(0, 4))

    # ── 日志 ────────────────────────────────────────────────────────
    def _append(self, text, color):
        self._log.configure(state="normal")
        self._log.insert("end", text, ("tag",))
        self._log.tag_config("tag", foreground=color)
        self._log.see("end")
        self._log.configure(state="disabled")

    def _clear(self):
        self._log.configure(state="normal")
        self._log.delete("1.0", "end")
        self._log.configure(state="disabled")

    def _toggle_hex(self):
        self._hex = not self._hex
        self._hex_btn.configure(
            text="HEX" if self._hex else "TXT",
            fg_color=ACCENT if self._hex else "transparent",
            text_color="#e0e0e0" if self._hex else MUTED,
            border_width=0 if self._hex else 1)

    # ── 事件循环 ────────────────────────────────────────────────────
    def _run_loop(self):
        asyncio.set_event_loop(self._loop)
        self._loop.run_forever()

    # ── 认证 ────────────────────────────────────────────────────────
    def _send_auth(self):
        if not self._key or self._authenticated:
            return
        self._auth_pending = False
        msg = f"[AUTH] {self._key}\n"
        self._append(f"[{_ts()}] → 发送认证密钥...\n", COLOR_INFO)
        asyncio.run_coroutine_threadsafe(
            self._do_send(msg.encode("utf-8")), self._loop)

    def _handle_bond(self, hex_key):
        self._auth_pending = False      # 取消可能待发的旧密钥
        self._save_key(hex_key)
        self._append(f"[{_ts()}] ✓ 已保存配对密钥\n", COLOR_INFO)
        self._send_auth()

    def _handle_auth_ok(self):
        self._authenticated = True
        self._set_status("已认证 ✓", ACCENT)

    def _handle_auth_fail(self):
        self._authenticated = False
        self._append(f"[{_ts()}] ✗ 认证失败！密钥不匹配\n", COLOR_ERR)
        self._set_status("认证失败", "#c0392b")

    def _unpair(self):
        if not self._client or not self._client.is_connected:
            self._append(f"[{_ts()}] 请先连接设备\n", COLOR_ERR)
            return
        self._append(f"[{_ts()}] → 发送解绑指令...\n", COLOR_INFO)
        asyncio.run_coroutine_threadsafe(
            self._do_send(b"[UNPAIR]\n"), self._loop)
        self._delete_key()
        self._authenticated = False
        self._append(f"[{_ts()}] ✓ 本地密钥已删除\n", COLOR_INFO)

    # ── 密码操作 ────────────────────────────────────────────────────
    def _query_pwd(self):
        if not self._client or not self._client.is_connected:
            self._append(f"[{_ts()}] 请先连接设备\n", COLOR_ERR)
            return
        self._append(f"[{_ts()}] → 查询门锁密码...\n", COLOR_PWD)
        asyncio.run_coroutine_threadsafe(
            self._do_send(b"[PWD_QUERY]\n"), self._loop)

    def _set_pwd(self):
        raw = self._pwd_entry.get().strip()
        if len(raw) != 6 or not raw.isdigit():
            self._append(f"[{_ts()}] 请输入6位数字密码\n", COLOR_ERR)
            return
        if not self._client or not self._client.is_connected:
            self._append(f"[{_ts()}] 请先连接设备\n", COLOR_ERR)
            return
        msg = f"[PWD_SET:{raw}]\n"
        self._append(f"[{_ts()}] → 设置密码: {raw}\n", COLOR_PWD)
        asyncio.run_coroutine_threadsafe(
            self._do_send(msg.encode()), self._loop)
        self._pwd_entry.delete(0, "end")

    # ── 连接管理 ────────────────────────────────────────────────────
    def _toggle(self):
        if self._client and self._client.is_connected:
            asyncio.run_coroutine_threadsafe(self._disconnect(), self._loop)
        else:
            asyncio.run_coroutine_threadsafe(self._connect(), self._loop)

    async def _connect(self):
        self.after(0, self._set_status, "正在扫描...", MUTED)
        dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10)
        if dev is None:
            self.after(0, self._append, f"[{_ts()}] 未找到 {DEVICE_NAME}\n", COLOR_ERR)
            self.after(0, self._set_status, "未连接", MUTED)
            return
        self.after(0, self._set_status, f"正在连接 {dev.address[:12]}...", MUTED)

        cli = BleakClient(dev.address,
                           disconnected_callback=self._on_disconnected)
        try:
            await cli.connect()
        except Exception as e:
            self.after(0, self._append, f"[{_ts()}] 连接失败: {e}\n", COLOR_ERR)
            self.after(0, self._set_status, "未连接", MUTED)
            return
        if not cli.is_connected:
            self.after(0, self._set_status, "未连接", MUTED)
            return

        self._authenticated = False

        # 注册 notify
        def rx_cb(_h, d):
            self.after(0, self._on_rx, bytes(d))

        try:
            await cli.start_notify(TX_UUID, rx_cb)
        except Exception as e:
            self.after(0, self._append, f"[{_ts()}] notify 失败: {e}\n", COLOR_ERR)
            await cli.disconnect()
            self.after(0, self._set_status, "未连接", MUTED)
            return

        self._client = cli
        self.after(0, self._set_connected, dev.address[:12])

        # 自动认证
        if self._key:
            self._auth_pending = True
            self.after(200, self._send_auth)   # 等 notify 就绪
        else:
            self.after(0, self._append,
                       f"[{_ts()}] 等待配对密钥...\n", COLOR_INFO)

    async def _disconnect(self):
        if self._client:
            try:
                await self._client.stop_notify(TX_UUID)
            except Exception:
                pass
            try:
                await self._client.disconnect()
            except Exception:
                pass
        self._client = None
        self.after(0, self._set_disconnected)

    # ── GUI 状态更新 ────────────────────────────────────────────────
    def _set_status(self, text, color):
        self._status.configure(text=text, text_color=color)

    def _set_connected(self, addr):
        self._dot.configure(text="●", text_color=ACCENT)
        self._status.configure(text=f"已连接 {addr}", text_color=ACCENT)
        self._btn.configure(text="断开", fg_color="#c0392b")
        self._append(f"[{_ts()}] 已连接 {addr}\n", COLOR_INFO)

    def _on_disconnected(self, client):
        """BleakClient 断开回调（自动断连或手动断开均触发）"""
        self.after(0, self._set_disconnected)

    def _set_disconnected(self):
        self._client = None
        self._authenticated = False
        self._auth_pending = False
        self._dot.configure(text="○", text_color=MUTED)
        self._status.configure(text="已断开", text_color=MUTED)
        self._btn.configure(text="连接", fg_color=ACCENT)
        self._append(f"[{_ts()}] 已断开\n", COLOR_INFO)

    # ── RX 数据 ─────────────────────────────────────────────────────
    def _on_rx(self, data: bytes):
        text = data.decode("utf-8", "replace")

        # 配对密钥
        if text.startswith("[BOND] "):
            hex_key = text[7:].strip()
            self._handle_bond(hex_key)
            return

        # 认证结果
        if text.strip() == "[OK]":
            self._handle_auth_ok()
            return
        if text.strip() == "[FAIL]":
            self._handle_auth_fail()
            return
        if text.strip() == "[UNPAIRED]":
            self._append(f"[{_ts()}] 设备端已解绑\n", COLOR_INFO)
            self._delete_key()
            return

        # 51 单片机密码回复
        if text.startswith("[PWD_DATA:"):
            pwd = text[10:].strip().rstrip("]")
            self._append(f"[{_ts()}] 门锁密码: {pwd}\n", COLOR_PWD)
            return
        if text.strip() == "[PWD_OK]":
            self._append(f"[{_ts()}] ✓ 密码修改成功\n", COLOR_PWD)
            return
        if text.strip() == "[PWD_ERR]":
            self._append(f"[{_ts()}] ✗ 密码修改失败\n", COLOR_ERR)
            return

        # 普通数据显示
        ts = _ts()
        if self._hex:
            self._append(f"[{ts}] RX: {data.hex(' ').upper()}\n", COLOR_RX)
        else:
            self._append(f"[{ts}] RX: {text}\n", COLOR_RX)

    # ── 发送 ────────────────────────────────────────────────────────
    async def _do_send(self, data):
        if not self._client or not self._client.is_connected:
            self.after(0, self._append, f"[{_ts()}] 未连接\n", COLOR_ERR)
            return
        try:
            await self._client.write_gatt_char(RX_UUID, data, response=False)
        except Exception as e:
            self.after(0, self._append, f"[{_ts()}] 发送失败: {e}\n", COLOR_ERR)

    def _send(self):
        raw = self._input.get("1.0", "end-1c").strip()
        if not raw:
            return
        if not self._authenticated:
            self._append(f"[{_ts()}] 等待认证完成\n", COLOR_ERR)
            return
        if self._hex:
            s = raw.replace(" ", "")
            if len(s) % 2:
                self._append(f"[{_ts()}] HEX 长度需为偶数\n", COLOR_ERR)
                return
            try:
                data = bytes.fromhex(s)
            except ValueError:
                self._append(f"[{_ts()}] HEX 格式无效\n", COLOR_ERR)
                return
        else:
            data = raw.encode("utf-8")

        ts = _ts()
        preview = data.hex(" ").upper() if self._hex else raw
        self._append(f"[{ts}] TX: {preview}\n", COLOR_TX)
        self._input.delete("1.0", "end")
        self._input.see("end")

        asyncio.run_coroutine_threadsafe(self._do_send(data), self._loop)

    def _on_enter(self, e):
        if not (e.state & 0x1):
            self._send()
            return "break"

    # ── 退出 ────────────────────────────────────────────────────────
    def _on_close(self):
        self._running = False
        if self._client and self._client.is_connected:
            fut = asyncio.run_coroutine_threadsafe(self._disconnect(), self._loop)
            try:
                fut.result(timeout=3)
            except Exception:
                pass
        self._loop.call_soon_threadsafe(self._loop.stop)
        self.destroy()


def _ts():
    return time.strftime("%H:%M:%S")


if __name__ == "__main__":
    logging.basicConfig(level=logging.WARNING)
    App().mainloop()
