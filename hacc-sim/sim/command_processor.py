from __future__ import annotations

import re
from typing import List, Optional

from .state import SimulatorState
from .persist import save_all, load_all


class CommandProcessor:
    def __init__(self, state: SimulatorState, log_queue=None) -> None:
        self.state = state
        self.log_queue = log_queue

    def _log(self, msg: str) -> None:
        if self.log_queue is not None:
            try:
                self.log_queue.put_nowait(msg)
            except Exception:
                pass

    def process(self, raw: bytes) -> List[str]:
        try:
            text = raw.decode("utf-8", errors="ignore").strip()
        except Exception:
            return ["Error: Invalid encoding"]

        if not text:
            return []

        cmd_lower = text.lower()

        # help (two parts)
        if cmd_lower == "help":
            return [
                "Available commands (Part 1/2):\nhelp\ndebug level <0-3>\ndebug status\nconfig\nconfig ssrlink <on/off>\nconfig ssrlink status\nconfig rgb0 <led_id> <r> <g> <b>\nconfig rgb0 status <led_id>\nconfig rgb100 <led_id> <r> <g> <b>\nconfig rgb100 status <led_id>\nconfig trans <ms>\nconfig trans status\nconfig ssr_freq <freq>\nconfig ssr_freq status\nconfig ssr_freq status <id>\nconfig load\nconfig save",
                "Available commands (Part 2/2):\nreboot\ninfo\nset <channel> <duty>\nget <channel>\nrgb <led_id> <r> <g> <b>\nrgbget <led_id>\nws2812 <system> <led_id> <r> <g> <b>\nws2812get <system> <led_id>\nws2812sys <system> <r> <g> <b>\nws2812off <system>\nfreq <channel> <freq>\nzerox",
            ]

        # debug level
        if cmd_lower.startswith("debug level "):
            try:
                level = int(cmd_lower.split()[2])
            except Exception:
                return ["Error: Invalid debug level. Must be 0-3"]
            if 0 <= level <= 3:
                self.state.config.debug_level = level
                return [f"Debug level set to: {level}"]
            return ["Error: Invalid debug level. Must be 0-3"]

        if cmd_lower == "debug status":
            return [f"Current debug level: {self.state.config.debug_level}"]

        # config root -> summary
        if cmd_lower == "config":
            c = self.state.config
            return [
                "Configuration:\n"
                f"SSR-LED Link: {'Enabled' if c.ssr_link_enabled else 'Disabled'}\n"
                f"Transition Time: {c.ssr_link_transition_ms} ms\n"
                f"Debug Level: {c.debug_level}"
            ]

        # config ssrlink
        if cmd_lower.startswith("config ssrlink "):
            val = cmd_lower.split()[2]
            if val in ("on", "1"):
                self.state.config.ssr_link_enabled = True
                # 既存のSSR値からRGBへ即時反映
                self.state.apply_ssr_link_all()
                return ["SSR-LED link enabled"]
            if val in ("off", "0"):
                self.state.config.ssr_link_enabled = False
                return ["SSR-LED link disabled"]
            if val == "status":
                return [f"SSR-LED link is {'enabled' if self.state.config.ssr_link_enabled else 'disabled'}"]
            return ["Error: Invalid command"]

        # config rgb0 / rgb100
        m = re.match(r"config\s+rgb(0|100)\s+(.*)$", cmd_lower)
        if m:
            which = m.group(1)
            args = m.group(2)
            if args.startswith("status "):
                try:
                    led_id = int(args.split()[1])
                except Exception:
                    return ["Error: Invalid command format"]
                if 1 <= led_id <= 4:
                    arr = self.state.config.rgb0 if which == "0" else self.state.config.rgb100
                    r, g, b = arr[led_id - 1]
                    suffix = "0%" if which == "0" else "100%"
                    return [f"LED{led_id} {suffix} color: R:{r} G:{g} B:{b}"]
                return ["Error: Invalid LED ID (1-4)"]
            else:
                # parse "<id>,<r>,<g>,<b>"
                try:
                    led_id, r, g, b = map(int, args.split(","))
                except Exception:
                    return ["Error: Invalid command format"]
                if not (1 <= led_id <= 4 and all(0 <= v <= 255 for v in (r, g, b))):
                    return ["Error: Invalid parameters"]
                arr = self.state.config.rgb0 if which == "0" else self.state.config.rgb100
                arr[led_id - 1] = (r, g, b)
                # 連動中なら即反映
                if self.state.config.ssr_link_enabled:
                    self.state.apply_ssr_link_for_id(led_id)
                suffix = "0%" if which == "0" else "100%"
                return [f"LED{led_id} {suffix} color set to R:{r} G:{g} B:{b}"]

        # config trans / config t
        if cmd_lower.startswith("config trans ") or cmd_lower.startswith("config t "):
            args = cmd_lower.split(maxsplit=2)[2]
            if args == "status":
                return [f"Transition time is {self.state.config.ssr_link_transition_ms} ms"]
            try:
                ms = int(args)
            except Exception:
                return ["Error: Invalid transition time. Must be 100-10000 ms"]
            if 100 <= ms <= 10000:
                self.state.config.ssr_link_transition_ms = ms
                return [f"Transition time set to {ms} ms"]
            return ["Error: Invalid transition time. Must be 100-10000 ms"]

        # config random rgb
        if cmd_lower.startswith("config random rgb status"):
            return [f"config random rgb status: {self.state.config.random_rgb_timeout_10s}"]
        if cmd_lower.startswith("config random rgb "):
            try:
                v = int(cmd_lower.split()[3])
            except Exception:
                return ["Error: Invalid value (0-255)"]
            if 0 <= v <= 255:
                self.state.config.random_rgb_timeout_10s = v
                return [f"config random rgb set to {v} (x10s)"]
            return ["Error: Invalid value (0-255)"]

        # config ssr_freq
        if cmd_lower.startswith("config ssr_freq "):
            tail = cmd_lower.split(maxsplit=2)[2]
            if tail.startswith("status "):
                try:
                    ssr_id = int(tail.split()[1])
                except Exception:
                    return ["Error: Invalid command format"]
                if 1 <= ssr_id <= 4:
                    freq = self.state.get_ssr_freq(ssr_id)
                    if freq == -1:
                        return [f"SSR{ssr_id} PWM frequency is -1 (設定変更無効)"]
                    return [f"SSR{ssr_id} PWM frequency is {freq} Hz"]
                return ["Error: Invalid SSR ID (1-4)"]
            if tail == "status":
                lines = ["SSR PWM frequencies:"]
                for i in range(1, 5):
                    f = self.state.get_ssr_freq(i)
                    lines.append(f"SSR{i}: {'-1 (設定変更無効)' if f == -1 else f'{f} Hz'}")
                return lines
            try:
                freq = int(tail)
            except Exception:
                return ["Error: Invalid frequency (-1-10 Hz)"]
            if -1 <= freq <= 10:
                self.state.set_ssr_freq_all(freq)
                if freq == -1:
                    return ["All SSR PWM frequencies set to -1 (設定変更無効)"]
                return [f"All SSR PWM frequencies set to {freq} Hz"]
            return ["Error: Invalid frequency (-1-10 Hz)"]

        if cmd_lower == "config load":
            ok = load_all(self.state)
            return ["Configuration loaded" if ok else "Configuration load failed or no data"]
        if cmd_lower == "config save":
            ok = save_all(self.state)
            return ["Configuration saved (including current SSR frequencies)" if ok else "Configuration save failed"]

        # set / ssr
        if cmd_lower.startswith("set ") or cmd_lower.startswith("ssr "):
            args = text.split(maxsplit=1)[1]
            # format: id,ON|OFF|value
            try:
                id_part, value_part = args.split(",", 1)
                ssr_id = int(id_part)
            except Exception:
                return [f"{text.split()[0]} {args},ERROR"]
            vp = value_part.strip()
            if vp.upper() == "ON":
                duty = 100
            elif vp.upper() == "OFF":
                duty = 0
            else:
                try:
                    duty = int(vp)
                except Exception:
                    return [f"{text.split()[0]} {args},ERROR"]
            if ssr_id == 0:
                ok = True
                for i in range(1, 5):
                    ok = self.state.set_ssr_duty(i, duty) and ok
                # SSR-LED連動
                if self.state.config.ssr_link_enabled:
                    self.state.apply_ssr_link_all()
            else:
                ok = self.state.set_ssr_duty(ssr_id, duty)
                if self.state.config.ssr_link_enabled:
                    self.state.apply_ssr_link_for_id(ssr_id)
            return [f"set {ssr_id},{duty},{'OK' if ok else 'ERROR'}"]

        # freq
        if cmd_lower.startswith("freq "):
            args = text.split(maxsplit=1)[1]
            try:
                ssr_id_s, freq_s = args.split(",", 1)
                ssr_id = int(ssr_id_s)
                freq = int(freq_s)
            except Exception:
                return [f"freq {args},ERROR"]
            if ssr_id == 0:
                ok = self.state.set_ssr_freq_all(freq)
            else:
                ok = self.state.set_ssr_freq(ssr_id, freq)
            return [f"freq {ssr_id},{freq},{'OK' if ok else 'ERROR'}"]

        # get
        if cmd_lower.startswith("get "):
            try:
                ssr_id = int(text.split()[1])
            except Exception:
                return ["get,ERROR"]
            if not (1 <= ssr_id <= 4):
                return ["get,ERROR"]
            duty = self.state.get_ssr_duty(ssr_id)
            freq = self.state.get_ssr_freq()
            return [f"get {ssr_id},{duty},{freq},OK"]

        # rgb
        if cmd_lower.startswith("rgb "):
            args = text.split(maxsplit=1)[1]
            try:
                led_id_s, r_s, g_s, b_s = args.split(",")
                led_id = int(led_id_s)
                r, g, b = int(r_s), int(g_s), int(b_s)
            except Exception:
                return [f"rgb {args},ERROR"]
            if led_id == 0:
                ok_all = True
                for i in range(1, 5):
                    ok_all = self.state.set_rgb(i, r, g, b) and ok_all
                ok = ok_all
            else:
                ok = self.state.set_rgb(led_id, r, g, b)
            return [f"rgb {led_id},{r},{g},{b},{'OK' if ok else 'ERROR'}"]

        # rgbget
        if cmd_lower.startswith("rgbget "):
            try:
                led_id = int(text.split()[1])
            except Exception:
                return ["rgbget,ERROR"]
            if not (1 <= led_id <= 4):
                return [f"rgbget {led_id},ERROR"]
            c = self.state.get_rgb(led_id)
            if c is None:
                return [f"rgbget {led_id},ERROR"]
            r, g, b = c
            return [f"rgbget {led_id},{r},{g},{b},OK"]

        # ws2812
        if cmd_lower.startswith("ws2812 "):
            args = text.split(maxsplit=1)[1]
            try:
                system_s, led_s, r_s, g_s, b_s = args.split(",")
                system = int(system_s)
                led = int(led_s)
                r, g, b = int(r_s), int(g_s), int(b_s)
            except Exception:
                return [f"ws2812 {args},ERROR"]
            ok = self.state.set_ws2812(system, led, r, g, b)
            return [f"ws2812 {system},{led},{r},{g},{b},{'OK' if ok else 'ERROR'}"]

        if cmd_lower.startswith("ws2812get "):
            args = text.split(maxsplit=1)[1]
            try:
                system_s, led_s = args.split(",")
                system = int(system_s)
                led = int(led_s)
            except Exception:
                return [f"ws2812get {args},ERROR"]
            c = self.state.get_ws2812(system, led)
            if c is None:
                return [f"ws2812get {system},{led},ERROR"]
            r, g, b = c
            return [f"ws2812get {system},{led},{r},{g},{b},OK"]

        if cmd_lower.startswith("ws2812sys "):
            args = text.split(maxsplit=1)[1]
            try:
                system_s, r_s, g_s, b_s = args.split(",")
                system = int(system_s)
                r, g, b = int(r_s), int(g_s), int(b_s)
            except Exception:
                return [f"ws2812sys {args},ERROR"]
            ok = self.state.set_ws2812_system(system, r, g, b)
            return [f"ws2812sys {system},{r},{g},{b},{'OK' if ok else 'ERROR'}"]

        if cmd_lower.startswith("ws2812off "):
            try:
                system = int(text.split()[1])
            except Exception:
                return ["ws2812off,ERROR"]
            ok = self.state.turn_off_ws2812(system)
            return [f"ws2812off {system},{'OK' if ok else 'ERROR'}"]

        # sofia
        if cmd_lower.startswith("sofia"):
            return ["sofia,KAWAII,OK"]

        # info
        if cmd_lower == "info":
            return ["info,ACpowerController,Ver 1.0.0,OK"]

        # mist
        if cmd_lower.startswith("mist "):
            try:
                duration = int(text.split()[1])
            except Exception:
                return [f"mist {text.split()[1] if len(text.split())>1 else ''},ERROR"]
            if 0 <= duration <= 10000:
                # Simulate LED1 white during mist; not timed here
                ok = self.state.set_rgb(1, 255, 255, 255)
                return [f"mist {duration},{'OK' if ok else 'ERROR'}"]
            return [f"mist {duration},ERROR"]

        # air
        if cmd_lower.startswith("air "):
            try:
                level = int(text.split()[1])
            except Exception:
                return ["air,ERROR"]
            if level not in (0, 1, 2):
                return [f"air {level},ERROR"]
            ok = True
            if level == 0:
                ok &= self.state.set_rgb(2, 0, 0, 0)
                ok &= self.state.set_rgb(3, 0, 0, 0)
            elif level == 1:
                ok &= self.state.set_rgb(2, 255, 255, 255)
                ok &= self.state.set_rgb(3, 0, 0, 0)
            else:
                ok &= self.state.set_rgb(2, 255, 255, 255)
                ok &= self.state.set_rgb(3, 255, 255, 255)
            return [f"air {level},{'OK' if ok else 'ERROR'}"]

        # zerox (stub)
        if cmd_lower == "zerox":
            return ["zerox,NOT_DETECTED,0,0,0.0,OK"]

        # reboot (no-op)
        if cmd_lower == "reboot":
            return ["reboot,OK"]

        return ["Error: Unknown command"]


