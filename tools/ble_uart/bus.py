"""线程安全事件总线 —— 直接从 demo6/tools/companion/bus.py 搬过来。"""
from __future__ import annotations

import asyncio
import logging
from typing import Any, Callable, Optional

logger = logging.getLogger(__name__)

Listener = Callable[[Any], None]


class EventBus:
    def __init__(self, loop: Optional[asyncio.AbstractEventLoop] = None) -> None:
        self._loop = loop
        self._listeners: dict[str, list[Listener]] = {}

    def set_loop(self, loop: asyncio.AbstractEventLoop) -> None:
        self._loop = loop

    def on(self, event: str, fn: Listener) -> Callable[[], None]:
        self._listeners.setdefault(event, []).append(fn)

        def _unsub() -> None:
            try:
                self._listeners[event].remove(fn)
            except (KeyError, ValueError):
                pass
        return _unsub

    def emit(self, event: str, payload: Any = None) -> None:
        for fn in list(self._listeners.get(event, ())):
            try:
                fn(payload)
            except Exception:
                logger.exception("event listener for %s crashed", event)

    def emit_threadsafe(self, event: str, payload: Any = None) -> None:
        if self._loop is None or self._loop.is_closed():
            return
        try:
            self._loop.call_soon_threadsafe(self.emit, event, payload)
        except RuntimeError:
            pass
