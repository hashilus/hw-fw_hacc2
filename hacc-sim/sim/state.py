from __future__ import annotations

from dataclasses import dataclass, field
from threading import RLock
from typing import List, Tuple
import time


RGBTuple = Tuple[int, int, int]


@dataclass
class Config:
	udp_port: int = 5555
	debug_level: int = 1
	ssr_link_enabled: bool = False
	ssr_link_transition_ms: int = 500
	random_rgb_timeout_10s: int = 0

	# SSR-LED link colors for 4 LEDs (1..4)
	rgb0: List[RGBTuple] = field(default_factory=lambda: [(0, 0, 0), (0, 0, 0), (0, 0, 0), (0, 0, 0)])
	rgb100: List[RGBTuple] = field(default_factory=lambda: [(255, 255, 255), (255, 255, 255), (255, 255, 255), (255, 255, 255)])


class SimulatorState:
	def __init__(self) -> None:
		self._lock = RLock()

		# SSR: 1..4
		self._ssr_duty: List[int] = [0, 0, 0, 0]
		# PWM frequency per SSR, -1..10
		self._ssr_freq: List[int] = [-1, -1, -1, -1]

		# RGB LEDs: 1..4, each (r,g,b)
		self._rgb: List[RGBTuple] = [(0, 0, 0) for _ in range(4)]
		# Transition state per LED
		self._transition_active: List[bool] = [False, False, False, False]
		self._transition_start_color: List[RGBTuple] = [(0, 0, 0) for _ in range(4)]
		self._transition_target_color: List[RGBTuple] = [(0, 0, 0) for _ in range(4)]
		self._transition_start_ms: List[int] = [0, 0, 0, 0]
		self._transition_duration_ms: List[int] = [0, 0, 0, 0]

		# WS2812: 3 systems, 256 LEDs each
		self._ws2812: List[List[RGBTuple]] = [
			[(0, 0, 0) for _ in range(256)] for _ in range(3)
		]

		self.config = Config()

	# ---------- SSR ----------
	def set_ssr_duty(self, ssr_id: int, duty: int) -> bool:
		if not (1 <= ssr_id <= 4 and 0 <= duty <= 100):
			return False
		with self._lock:
			self._ssr_duty[ssr_id - 1] = duty
		return True

	def get_ssr_duty(self, ssr_id: int) -> int:
		with self._lock:
			return self._ssr_duty[ssr_id - 1]

	def set_ssr_freq_all(self, freq: int) -> bool:
		if not (-1 <= freq <= 10):
			return False
		with self._lock:
			for i in range(4):
				self._ssr_freq[i] = freq
		return True

	def set_ssr_freq(self, ssr_id: int, freq: int) -> bool:
		if not (1 <= ssr_id <= 4 and -1 <= freq <= 10):
			return False
		with self._lock:
			self._ssr_freq[ssr_id - 1] = freq
		return True

	def get_ssr_freq(self, ssr_id: int | None = None) -> int:
		with self._lock:
			if ssr_id is None:
				# Default: return common view (SSR1)
				return self._ssr_freq[0]
			return self._ssr_freq[ssr_id - 1]

	# ---------- RGB ----------
	def set_rgb(self, led_id: int, r: int, g: int, b: int) -> bool:
		if not (1 <= led_id <= 4 and all(0 <= v <= 255 for v in (r, g, b))):
			return False
		with self._lock:
			self._rgb[led_id - 1] = (r, g, b)
			# Cancel transition for this LED if any
			self._transition_active[led_id - 1] = False
		return True

	def get_rgb(self, led_id: int) -> Tuple[int, int, int] | None:
		if not (1 <= led_id <= 4):
			return None
		with self._lock:
			self._update_transitions_locked()
			return self._rgb[led_id - 1]

	# ---------- SSR-LED link ----------
	def calculate_led_color_for_ssr(self, led_id: int, duty: int) -> RGBTuple:
		# Linear interpolation between rgb0 and rgb100
		duty = max(0, min(100, duty))
		c0 = self.config.rgb0[led_id - 1]
		c1 = self.config.rgb100[led_id - 1]
		r = c0[0] + (c1[0] - c0[0]) * duty // 100
		g = c0[1] + (c1[1] - c0[1]) * duty // 100
		b = c0[2] + (c1[2] - c0[2]) * duty // 100
		return (int(r), int(g), int(b))

	def apply_ssr_link_for_id(self, led_id: int) -> None:
		if not (1 <= led_id <= 4):
			return
		if not self.config.ssr_link_enabled:
			return
		with self._lock:
			duty = self._ssr_duty[led_id - 1]
			target = self.calculate_led_color_for_ssr(led_id, duty)
			duration = self.config.ssr_link_transition_ms
			if duration <= 0:
				self._rgb[led_id - 1] = target
				self._transition_active[led_id - 1] = False
			else:
				self._start_transition_locked(led_id - 1, target, duration)

	def apply_ssr_link_all(self) -> None:
		if not self.config.ssr_link_enabled:
			return
		for i in range(1, 5):
			self.apply_ssr_link_for_id(i)

	# ---------- WS2812 ----------
	def set_ws2812(self, system: int, led_id: int, r: int, g: int, b: int) -> bool:
		if not (1 <= system <= 3 and 1 <= led_id <= 256 and all(0 <= v <= 255 for v in (r, g, b))):
			return False
		with self._lock:
			self._ws2812[system - 1][led_id - 1] = (r, g, b)
		return True

	def set_ws2812_system(self, system: int, r: int, g: int, b: int) -> bool:
		if not (1 <= system <= 3 and all(0 <= v <= 255 for v in (r, g, b))):
			return False
		with self._lock:
			self._ws2812[system - 1] = [(r, g, b) for _ in range(256)]
		return True

	def turn_off_ws2812(self, system: int) -> bool:
		return self.set_ws2812_system(system, 0, 0, 0)

	def get_ws2812(self, system: int, led_id: int) -> Tuple[int, int, int] | None:
		if not (1 <= system <= 3 and 1 <= led_id <= 256):
			return None
		with self._lock:
			return self._ws2812[system - 1][led_id - 1]

	# ---------- Snapshots for GUI ----------
	def snapshot(self) -> dict:
		with self._lock:
			self._update_transitions_locked()
			return {
				"ssr_duty": list(self._ssr_duty),
				"ssr_freq": list(self._ssr_freq),
				"rgb": list(self._rgb),
				# Downsample WS2812 preview as average color per system for GUI
				"ws2812_avg": [
					self._avg_color(system)
					for system in range(3)
				],
				"config": self.config,
			}

	def _avg_color(self, system_index: int) -> RGBTuple:
		arr = self._ws2812[system_index]
		if not arr:
			return (0, 0, 0)
		r = sum(c[0] for c in arr) // len(arr)
		g = sum(c[1] for c in arr) // len(arr)
		b = sum(c[2] for c in arr) // len(arr)
		return (r, g, b)

	# ---------- Transition helpers ----------
	def _now_ms(self) -> int:
		return int(time.monotonic() * 1000)

	def _start_transition_locked(self, led_index: int, target: RGBTuple, duration_ms: int) -> None:
		start = self._rgb[led_index]
		self._transition_active[led_index] = True
		self._transition_start_color[led_index] = start
		self._transition_target_color[led_index] = target
		self._transition_start_ms[led_index] = self._now_ms()
		self._transition_duration_ms[led_index] = max(0, int(duration_ms))

	def _update_transitions_locked(self) -> None:
		now = self._now_ms()
		for i in range(4):
			if not self._transition_active[i]:
				continue
			dur = self._transition_duration_ms[i]
			if dur <= 0:
				self._rgb[i] = self._transition_target_color[i]
				self._transition_active[i] = False
				continue
			t = (now - self._transition_start_ms[i]) / dur
			if t >= 1.0:
				self._rgb[i] = self._transition_target_color[i]
				self._transition_active[i] = False
				continue
			sr, sg, sb = self._transition_start_color[i]
			tr, tg, tb = self._transition_target_color[i]
			cr = int(sr + (tr - sr) * t)
			cg = int(sg + (tg - sg) * t)
			cb = int(sb + (tb - sb) * t)
			self._rgb[i] = (cr, cg, cb)


