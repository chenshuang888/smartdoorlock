"""BLE 核心 —— 参考 demo6/tools/companion/core.py。

负责扫描、连接、notify 注册、串行化写。
通过 EventBus 向外发事件："connect", "disconnect", "notify", "log"。
"""
from __future__ import annotations

import asyncio
import logging
from typing import Optional

from bleak import BleakClient, BleakScanner

from .bus import EventBus
from .constants import DEVICE_NAME, RECONNECT_DELAY, SCAN_TIMEOUT, TX_UUID, RX_UUID

logger = logging.getLogger(__name__)


class BLEConnection:
    def __init__(self, bus: EventBus) -> None:
        self._bus = bus
        self._client: Optional[BleakClient] = None
        self._write_lock = asyncio.Lock()
        self._stopped = asyncio.Event()
        self._loop: Optional[asyncio.AbstractEventLoop] = None

    @property
    def is_connected(self) -> bool:
        return self._client is not None and self._client.is_connected

    async def write(self, data: bytes, response: bool = False) -> None:
        if self._client is None or not self._client.is_connected:
            raise RuntimeError("not connected")
        async with self._write_lock:
            await self._client.write_gatt_char(RX_UUID, data, response=response)

    # ── 主循环 ──────────────────────────────────────────────────

    async def run(self) -> None:
        while not self._stopped.is_set():
            try:
                await self._connect_once()
                await self._stay_connected()
            except asyncio.CancelledError:
                break
            except Exception as e:
                self._bus.emit("log", ("warn", f"连接断开: {e}"))
            finally:
                await self._teardown()

            if self._stopped.is_set():
                break

            self._bus.emit("log", ("info", f"{RECONNECT_DELAY:.0f}s 后重连..."))
            try:
                await asyncio.wait_for(self._stopped.wait(), timeout=RECONNECT_DELAY)
                break
            except asyncio.TimeoutError:
                pass

    def stop(self) -> None:
        self._stopped.set()

    # ── 内部 ─────────────────────────────────────────────────────

    async def _connect_once(self) -> None:
        self._bus.emit("log", ("info", f"扫描 {DEVICE_NAME}..."))
        device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=SCAN_TIMEOUT)
        if device is None:
            raise RuntimeError(f"{SCAN_TIMEOUT:.0f}s 内未发现 {DEVICE_NAME}")

        addr = device.address
        self._bus.emit("log", ("info", f"连接 {addr[:12]}..."))
        cli = BleakClient(addr)
        await cli.connect()
        if not cli.is_connected:
            raise RuntimeError("连接失败")

        self._client = cli
        self._bus.emit("connect", addr)

    async def _stay_connected(self) -> None:
        # 注册 notify
        def rx_cb(_handle: int, data: bytearray) -> None:
            self._bus.emit_threadsafe("notify", bytes(data))

        try:
            await self._client.start_notify(TX_UUID, rx_cb)
        except Exception as e:
            raise RuntimeError(f"notify 注册失败: {e}")

        self._bus.emit("log", ("info", "BLE 就绪，等待数据..."))

        # 循环检查连接状态
        while not self._stopped.is_set():
            if self._client is None or not self._client.is_connected:
                raise RuntimeError("连接断开")
            try:
                await asyncio.wait_for(self._stopped.wait(), timeout=2.0)
                return
            except asyncio.TimeoutError:
                pass

    async def _teardown(self) -> None:
        if self._client:
            try:
                if self._client.is_connected:
                    await self._client.disconnect()
            except Exception:
                pass
        self._client = None
        self._bus.emit("disconnect", None)
