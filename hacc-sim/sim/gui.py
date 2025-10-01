from __future__ import annotations

import tkinter as tk
from tkinter import ttk
from typing import Tuple

from .state import SimulatorState
from .command_processor import CommandProcessor
from .udp_server import UDPServer


class SimulatorGUI:
    def __init__(self, state: SimulatorState, processor: CommandProcessor, log_queue=None) -> None:
        self.state = state
        self.processor = processor
        self.log_queue = log_queue
        self.udp: UDPServer | None = None

        self.root = tk.Tk()
        self.root.title("HACC2 Simulator")
        self.root.geometry("840x600")

        # Top control bar (port + connect/disconnect)
        ctrl = ttk.Frame(self.root)
        ttk.Label(ctrl, text="UDP Port:").pack(side=tk.LEFT, padx=(8, 4))
        self.port_var = tk.StringVar(value=str(self.state.config.udp_port))
        self.port_entry = ttk.Entry(ctrl, textvariable=self.port_var, width=8)
        self.port_entry.pack(side=tk.LEFT)
        self.conn_btn = ttk.Button(ctrl, text="Connect", command=self._toggle_conn)
        self.conn_btn.pack(side=tk.LEFT, padx=6)
        self.status_lbl = ttk.Label(ctrl, text="(stopped)")
        self.status_lbl.pack(side=tk.LEFT, padx=8)
        ctrl.pack(fill=tk.X, padx=6, pady=(6, 0))

        # Layout panes
        main = ttk.Panedwindow(self.root, orient=tk.VERTICAL)
        main.pack(fill=tk.BOTH, expand=True)

        top = ttk.Panedwindow(main, orient=tk.HORIZONTAL)
        bottom = ttk.Panedwindow(main, orient=tk.HORIZONTAL)
        main.add(top, weight=3)
        main.add(bottom, weight=2)

        # SSR panel (display only)
        self._ssr_bars = []
        ssr_frame = ttk.Labelframe(top, text="SSR")
        for i in range(4):
            col = ttk.Frame(ssr_frame)
            ttk.Label(col, text=f"SSR{i+1}").pack()
            bar = ttk.Progressbar(col, orient=tk.VERTICAL, length=160, mode="determinate", maximum=100)
            bar.pack(padx=8, pady=4)
            self._ssr_bars.append(bar)
            col.pack(side=tk.LEFT, padx=6, pady=6)
        top.add(ssr_frame, weight=1)

        # RGB panel (display only)
        self._rgb_canv = []
        rgb_frame = ttk.Labelframe(top, text="RGB LEDs")
        for i in range(4):
            cell = ttk.Frame(rgb_frame)
            ttk.Label(cell, text=f"LED{i+1}").pack()
            canv = tk.Canvas(cell, width=64, height=64, bd=1, relief=tk.SUNKEN)
            rect = canv.create_rectangle(0, 0, 64, 64, fill="#000000", outline="")
            canv.pack(padx=8, pady=4)
            self._rgb_canv.append((canv, rect))
            cell.pack(side=tk.LEFT, padx=6, pady=6)
        top.add(rgb_frame, weight=1)

        # Log panel (read-only)
        console = ttk.Labelframe(bottom, text="Log (UDP RX/TX)")
        self.text = tk.Text(console, height=10, wrap=tk.WORD, state=tk.NORMAL)
        self.text.pack(fill=tk.BOTH, expand=True, padx=6, pady=(6, 6))
        bottom.add(console, weight=1)

        # periodic update
        self.root.after(100, self._tick)

    def _append(self, line: str) -> None:
        self.text.insert(tk.END, line + "\n")
        self.text.see(tk.END)

    def _tick(self) -> None:
        # Drain log queue
        if self.log_queue is not None:
            try:
                while True:
                    msg = self.log_queue.get_nowait()
                    self._append(msg)
            except Exception:
                pass

        snap = self.state.snapshot()
        for i, bar in enumerate(self._ssr_bars):
            bar["value"] = snap["ssr_duty"][i]

        for i, (canv, rect) in enumerate(self._rgb_canv):
            r, g, b = snap["rgb"][i]
            canv.itemconfig(rect, fill=f"#{r:02x}{g:02x}{b:02x}")

        self.root.after(100, self._tick)

        # Update status label
        if self.udp is None:
            self.status_lbl.configure(text="(stopped)")
        else:
            self.status_lbl.configure(text=f"listening : {self.udp.port}")

    def run(self) -> None:
        self._append("Type 'help' for commands. UDP server also accepts the same commands on port 5555.")
        self.root.mainloop()

    def _toggle_conn(self) -> None:
        if self.udp is None:
            # start
            try:
                port = int(self.port_var.get())
            except Exception:
                self._append("Invalid port")
                return
            self.state.config.udp_port = port
            self.udp = UDPServer(port=port, processor=self.processor, log_queue=self.log_queue)
            self.udp.start()
            self.conn_btn.configure(text="Disconnect")
            self._append(f"UDP listening on 0.0.0.0:{port}")
        else:
            self.udp.stop()
            self.udp = None
            self.conn_btn.configure(text="Connect")
            self._append("UDP stopped")


