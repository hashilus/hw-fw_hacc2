from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Dict, List, Tuple

from .state import SimulatorState


def _data_path() -> Path:
    # Save JSON next to app.py (project root)
    root = Path(__file__).resolve().parent.parent
    return root / "sim_data.json"


def save_all(state: SimulatorState) -> bool:
    try:
        cfg = state.config
        data: Dict[str, Any] = {
            "config": {
                "udp_port": cfg.udp_port,
                "debug_level": cfg.debug_level,
                "ssr_link_enabled": cfg.ssr_link_enabled,
                "ssr_link_transition_ms": cfg.ssr_link_transition_ms,
                "random_rgb_timeout_10s": cfg.random_rgb_timeout_10s,
                "rgb0": list(cfg.rgb0),
                "rgb100": list(cfg.rgb100),
                "ssr_freq": [state.get_ssr_freq(i + 1) for i in range(4)],
            },
            "state": {
                "ssr_duty": [state.get_ssr_duty(i + 1) for i in range(4)],
                "rgb": [list(state.get_rgb(i + 1) or (0, 0, 0)) for i in range(4)],
                # Persist WS2812 fully; size is modest (3*256)
                "ws2812": [
                    [list(state.get_ws2812(sys + 1, led + 1) or (0, 0, 0)) for led in range(256)]
                    for sys in range(3)
                ],
            },
        }
        path = _data_path()
        path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
        return True
    except Exception:
        return False


def load_all(state: SimulatorState) -> bool:
    try:
        path = _data_path()
        if not path.exists():
            return False
        obj = json.loads(path.read_text(encoding="utf-8"))
        cfg = obj.get("config", {})
        st = obj.get("state", {})

        # Config
        if "udp_port" in cfg:
            state.config.udp_port = int(cfg["udp_port"])  # not used to bind currently
        if "debug_level" in cfg:
            state.config.debug_level = int(cfg["debug_level"])
        if "ssr_link_enabled" in cfg:
            state.config.ssr_link_enabled = bool(cfg["ssr_link_enabled"])
        if "ssr_link_transition_ms" in cfg:
            state.config.ssr_link_transition_ms = int(cfg["ssr_link_transition_ms"])
        if "random_rgb_timeout_10s" in cfg:
            state.config.random_rgb_timeout_10s = int(cfg["random_rgb_timeout_10s"])
        if "rgb0" in cfg:
            state.config.rgb0 = [tuple(map(int, t)) for t in cfg["rgb0"]][:4]
        if "rgb100" in cfg:
            state.config.rgb100 = [tuple(map(int, t)) for t in cfg["rgb100"]][:4]
        if "ssr_freq" in cfg:
            arr = list(cfg["ssr_freq"])[:4]
            for i, f in enumerate(arr, start=1):
                try:
                    state.set_ssr_freq(i, int(f))
                except Exception:
                    pass

        # State
        if "ssr_duty" in st:
            for i, d in enumerate(list(st["ssr_duty"])[:4], start=1):
                try:
                    state.set_ssr_duty(i, int(d))
                except Exception:
                    pass
        if "rgb" in st:
            for i, c in enumerate(list(st["rgb"])[:4], start=1):
                try:
                    r, g, b = map(int, c)
                    state.set_rgb(i, r, g, b)
                except Exception:
                    pass
        if "ws2812" in st:
            ws = st["ws2812"]
            for sys_idx in range(min(3, len(ws))):
                row = ws[sys_idx]
                for led_idx in range(min(256, len(row))):
                    try:
                        r, g, b = map(int, row[led_idx])
                        state.set_ws2812(sys_idx + 1, led_idx + 1, r, g, b)
                    except Exception:
                        pass
        return True
    except Exception:
        return False


