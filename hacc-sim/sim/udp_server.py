from __future__ import annotations

import socket
import time
from typing import Optional
import threading

from .command_processor import CommandProcessor


class UDPServer:
    def __init__(
        self,
        port: int,
        processor: CommandProcessor,
        log_queue=None,
        response_queue=None,
    ) -> None:
        self.port = port
        self.processor = processor
        self.log_queue = log_queue
        self.response_queue = response_queue
        self._thread: Optional[threading.Thread] = None
        self._stop = threading.Event()

    def _log(self, msg: str) -> None:
        if self.log_queue is not None:
            try:
                self.log_queue.put_nowait(msg)
            except Exception:
                pass

    def _loop(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            sock.bind(("0.0.0.0", self.port))
        except OSError as e:
            self._log(f"UDP bind error on {self.port}: {e}")
            try:
                sock.close()
            except Exception:
                pass
            return
        sock.settimeout(0.2)
        self._log(f"UDP listening on 0.0.0.0:{self.port}")

        try:
            while not self._stop.is_set():
                try:
                    data, addr = sock.recvfrom(2048)
                except socket.timeout:
                    continue
                except OSError as e:
                    self._log(f"UDP recv error: {e}")
                    time.sleep(0.05)
                    continue

                self._log(f"UDP packet from {addr[0]}:{addr[1]} ({len(data)} bytes)")
                preview = data.decode("utf-8", errors="ignore")
                if len(preview) > 200:
                    preview = preview[:200] + "…"
                self._log(f"RX: {preview}")

                responses = self.processor.process(data)
                for resp in responses:
                    try:
                        sock.sendto(resp.encode("utf-8"), addr)
                        self._log(f"TX: {resp}")
                    except OSError as e:
                        self._log(f"UDP send error: {e}")
        finally:
            try:
                sock.close()
            except Exception:
                pass

    def start(self, port: Optional[int] = None) -> bool:
        if self._thread and self._thread.is_alive():
            self._log("UDP already running")
            return True
        if port is not None:
            self.port = int(port)
        self._stop.clear()
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()
        return True

    def stop(self) -> None:
        self._stop.set()
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=1.0)
        self._thread = None


