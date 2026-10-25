#!/usr/bin/env python3
"""Native Robo Dash simulator using the exact firmware C game core."""

from __future__ import annotations

import argparse
import ctypes as C
import os
from pathlib import Path
import platform
import shlex
import subprocess
import sys
import time
import tkinter as tk

ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build" / "simulator"
LIB_PATH = BUILD_DIR / ("librunner_game.dylib" if sys.platform == "darwin" else "librunner_game.so")
HIGH_SCORE_PATH = BUILD_DIR / "high_score.txt"
SCALE = 2
WIDTH = 240
HEIGHT = 320
TICK_SECONDS = 0.020

MODE_TITLE = 0
MODE_RUNNING = 1
MODE_PAUSED = 2
MODE_GAME_OVER = 3
MODE_FINISHED = 4
DURATION_TICKS = 3000
INPUT_START = 0
INPUT_LEFT = 1
INPUT_RIGHT = 2
INPUT_PAUSE = 3
INPUT_EXIT = 4
INPUT_JUMP = 5
INPUT_OVERLOAD = 6
EVENT_HIT = 1 << 3
EVENT_WIN = 1 << 4
CP_SCALE = 100

BG = "#080D1C"
PANEL = "#111C33"
GRID = "#24415E"
CYAN = "#51F0DC"
BLUE = "#3185D8"
GOLD = "#FFD166"
TEXT = "#F2FAFF"
MUTED = "#89A5BD"
DANGER = "#FF637D"
DANGER_DARK = "#8D304B"
SHADOW = "#050914"
STAGE_ACCENTS = (CYAN, "#B99AFF", "#FFAB5E")
STAGE_ROADS = ("#132A40", "#211E43", "#3A2535")


class RunnerRect(C.Structure):
    _fields_ = [(name, C.c_int32) for name in ("x", "y", "w", "h")]


class RunnerObstacle(C.Structure):
    _fields_ = [("y", C.c_int32), ("lane", C.c_uint8), ("kind", C.c_uint8),
                ("active", C.c_bool), ("cleared", C.c_bool)]


class RunnerCoin(C.Structure):
    _fields_ = [("y", C.c_int32), ("lane", C.c_uint8), ("value", C.c_uint8),
                ("height", C.c_uint8), ("active", C.c_bool)]


class RunnerPowerup(C.Structure):
    _fields_ = [("y", C.c_int32), ("lane", C.c_uint8), ("kind", C.c_uint8),
                ("active", C.c_bool)]


class RunnerGame(C.Structure):
    _fields_ = [
        ("mode", C.c_int),
        ("player_x", C.c_int32),
        ("lane", C.c_uint8),
        ("safe_lane", C.c_uint8),
        ("safe_lane_hold", C.c_uint8),
        ("lives", C.c_uint8),
        ("combo", C.c_uint8),
        ("multiplier", C.c_uint8),
        ("stage", C.c_uint8),
        ("recovery_rows", C.c_uint8),
        ("invincible_ticks", C.c_uint8),
        ("hit_flash_ticks", C.c_uint8),
        ("coin_flash_ticks", C.c_uint8),
        ("stage_banner_ticks", C.c_uint8),
        ("jump_ticks", C.c_uint8),
        ("last_powerup", C.c_uint8),
        ("power_flash_ticks", C.c_uint8),
        ("pattern_index", C.c_uint8),
        ("mission_flash_ticks", C.c_uint8),
        ("mission_complete", C.c_bool),
        ("energy", C.c_uint8),
        ("route_choice", C.c_uint8),
        ("last_route", C.c_uint8),
        ("route_risk_rows", C.c_uint8),
        ("route_safe_rows", C.c_uint8),
        ("boss_hp", C.c_uint8),
        ("boss_attack_mask", C.c_uint8),
        ("route_offer", C.c_bool),
        ("boss_active", C.c_bool),
        ("boss_vulnerable", C.c_bool),
        ("boss_overload_hit", C.c_bool),
        ("shield_ticks", C.c_uint16),
        ("rush_ticks", C.c_uint16),
        ("overload_ticks", C.c_uint16),
        ("route_ticks", C.c_uint16),
        ("boss_phase_ticks", C.c_uint16),
        ("boss_flash_ticks", C.c_uint16),
        ("obstacles", RunnerObstacle * 16),
        ("coins", RunnerCoin * 40),
        ("powerups", RunnerPowerup * 4),
        ("score", C.c_uint32),
        ("high_score", C.c_uint32),
        ("coin_count", C.c_uint32),
        ("bonus_score", C.c_uint32),
        ("coin_feedback", C.c_uint32),
        ("elapsed_ticks", C.c_uint32),
        ("jump_count", C.c_uint32),
        ("distance", C.c_int32),
        ("spawn_ticks", C.c_int32),
        ("powerup_spawn_ticks", C.c_int32),
        ("rng_state", C.c_uint32),
        ("events", C.c_uint32),
        ("new_high_score", C.c_bool),
    ]


def build_library(force: bool = False) -> Path:
    source = ROOT / "main" / "runner_game.c"
    header = ROOT / "main" / "runner_game.h"
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    if not force and LIB_PATH.exists():
        library_time = LIB_PATH.stat().st_mtime_ns
        if all(path.stat().st_mtime_ns <= library_time for path in (source, header)):
            return LIB_PATH
    compiler = shlex.split(os.environ.get("CC", "cc"))
    shared_flag = "-dynamiclib" if platform.system() == "Darwin" else "-shared"
    command = compiler + [
        "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-fPIC", shared_flag,
        "-Imain", str(source), "-o", str(LIB_PATH),
    ]
    subprocess.run(command, cwd=ROOT, check=True)
    return LIB_PATH


def load_core(path: Path) -> C.CDLL:
    lib = C.CDLL(str(path))
    game_pointer = C.POINTER(RunnerGame)
    lib.runner_game_init.argtypes = [game_pointer, C.c_uint32]
    lib.runner_game_start.argtypes = [game_pointer, C.c_uint32]
    lib.runner_game_input.argtypes = [game_pointer, C.c_int, C.c_uint32]
    lib.runner_game_tick.argtypes = [game_pointer]
    lib.runner_game_take_events.argtypes = [game_pointer]
    lib.runner_game_take_events.restype = C.c_uint32
    lib.runner_game_player_rect.argtypes = [game_pointer]
    lib.runner_game_player_rect.restype = RunnerRect
    lib.runner_game_obstacle_rect.argtypes = [C.POINTER(RunnerObstacle)]
    lib.runner_game_obstacle_rect.restype = RunnerRect
    lib.runner_game_coin_rect.argtypes = [C.POINTER(RunnerCoin)]
    lib.runner_game_coin_rect.restype = RunnerRect
    lib.runner_game_powerup_rect.argtypes = [C.POINTER(RunnerPowerup)]
    lib.runner_game_powerup_rect.restype = RunnerRect
    lib.runner_game_lane_center.argtypes = [C.c_uint8]
    lib.runner_game_lane_center.restype = C.c_int32
    lib.runner_game_jump_height.argtypes = [game_pointer]
    lib.runner_game_jump_height.restype = C.c_int32
    return lib


def check_core(lib: C.CDLL) -> None:
    game = RunnerGame()
    lib.runner_game_init(C.byref(game), 42)
    assert game.mode == MODE_TITLE and game.high_score == 42
    assert game.lives == 3 and game.multiplier == 1
    lib.runner_game_input(C.byref(game), INPUT_START, 123)
    lib.runner_game_take_events(C.byref(game))
    game.spawn_ticks = 100000
    game.powerup_spawn_ticks = 100000
    lib.runner_game_input(C.byref(game), INPUT_LEFT, 123)
    for _ in range(6):
        lib.runner_game_tick(C.byref(game))
    assert game.mode == MODE_RUNNING and game.lane == 0
    assert game.player_x == lib.runner_game_lane_center(0)
    rect = lib.runner_game_player_rect(C.byref(game))
    assert rect.x == 36 and rect.w == 24 and rect.h == 36
    lib.runner_game_input(C.byref(game), INPUT_JUMP, 123)
    for _ in range(17):
        lib.runner_game_tick(C.byref(game))
    assert lib.runner_game_jump_height(C.byref(game)) == 40
    game.energy = 100
    lib.runner_game_input(C.byref(game), INPUT_OVERLOAD, 123)
    assert game.overload_ticks == 150 and game.energy == 0
    lib.runner_game_start(C.byref(game), 123)
    game.spawn_ticks = 100000
    game.powerup_spawn_ticks = 100000
    game.coins[0].y = 214 * CP_SCALE
    game.coins[0].lane = 0
    game.coins[0].value = 1
    game.coins[0].active = True
    lib.runner_game_tick(C.byref(game))
    assert game.coins[0].active and game.coin_count == 0
    rect = lib.runner_game_coin_rect(C.byref(game.coins[0]))
    assert rect.x == lib.runner_game_lane_center(0) // CP_SCALE - 6
    lib.runner_game_start(C.byref(game), 123)
    game.spawn_ticks = 100000
    game.powerup_spawn_ticks = 100000
    game.coins[0].y = 214 * CP_SCALE
    game.coins[0].lane = 1
    game.coins[0].value = 1
    game.coins[0].active = True
    lib.runner_game_tick(C.byref(game))
    assert not game.coins[0].active and game.coin_count == 1
    print("Simulator core: PASS (lane, jump, overload and contact-only coin collection)")


class RunnerSimulator(tk.Tk):
    def __init__(self, lib: C.CDLL) -> None:
        super().__init__()
        self.lib = lib
        self.game = RunnerGame()
        self.lib.runner_game_init(C.byref(self.game), self.read_high_score())
        self.title("Robo Dash - Local Simulator")
        self.resizable(False, False)
        self.configure(bg=BG)
        self.canvas = tk.Canvas(
            self, width=WIDTH * SCALE, height=HEIGHT * SCALE,
            bg=BG, highlightthickness=0, cursor="arrow",
        )
        self.canvas.pack()
        self.held: set[str] = set()
        self.dirty = True
        self.next_tick = time.monotonic() + TICK_SECONDS
        self.protocol("WM_DELETE_WINDOW", self.destroy)
        self.bind("<KeyPress>", self.on_key_press)
        self.bind("<KeyRelease>", self.on_key_release)
        self.bind("<FocusOut>", self.on_focus_out)
        self.after(0, self.loop)
        self.after(80, self.focus_force)

    def read_high_score(self) -> int:
        try:
            return max(0, int(HIGH_SCORE_PATH.read_text(encoding="ascii").strip()))
        except (OSError, ValueError):
            return 0

    def save_high_score(self) -> None:
        try:
            HIGH_SCORE_PATH.write_text(f"{self.game.high_score}\n", encoding="ascii")
        except OSError as error:
            print(f"High-score save failed: {error}", file=sys.stderr)

    def send(self, action: int) -> None:
        self.lib.runner_game_input(C.byref(self.game), action, time.time_ns() & 0xFFFFFFFF)
        self.dirty = True

    def on_key_press(self, event: tk.Event) -> None:
        key = event.keysym
        if key in self.held:
            return
        self.held.add(key)
        if key in ("space", "Return"):
            action = INPUT_JUMP if self.game.mode == MODE_RUNNING else INPUT_START
            self.send(action)
        elif key in ("p", "P"):
            if self.game.mode in (MODE_RUNNING, MODE_PAUSED):
                self.send(INPUT_PAUSE)
        elif key in ("x", "X"):
            self.send(INPUT_OVERLOAD)
        elif key in ("Up", "Left", "a", "A"):
            self.send(INPUT_LEFT)
        elif key in ("Down", "Right", "d", "D"):
            exits = self.game.mode in (MODE_PAUSED, MODE_GAME_OVER, MODE_FINISHED)
            self.send(INPUT_EXIT if exits and key == "Down" else INPUT_RIGHT)
        elif key == "Escape":
            self.destroy()

    def on_key_release(self, event: tk.Event) -> None:
        self.held.discard(event.keysym)

    def on_focus_out(self, _event: tk.Event) -> None:
        self.held.clear()
        if self.game.mode == MODE_RUNNING:
            self.send(INPUT_PAUSE)

    def loop(self) -> None:
        if not self.winfo_exists():
            return
        now = time.monotonic()
        steps = 0
        while now >= self.next_tick and steps < 5:
            self.lib.runner_game_tick(C.byref(self.game))
            self.next_tick += TICK_SECONDS
            steps += 1
        if now >= self.next_tick:
            self.next_tick = now + TICK_SECONDS
        events = self.lib.runner_game_take_events(C.byref(self.game))
        if events & (EVENT_HIT | EVENT_WIN) and self.game.new_high_score:
            self.save_high_score()
        if steps or self.dirty:
            self.draw()
            self.dirty = False
        self.after(5, self.loop)

    @staticmethod
    def box(value: int | float) -> int:
        return round(value * SCALE)

    def rectangle(self, x: int, y: int, w: int, h: int, color: str,
                  outline: str = "", width: int = 1) -> None:
        self.canvas.create_rectangle(
            self.box(x), self.box(y), self.box(x + w), self.box(y + h),
            fill=color, outline=outline, width=self.box(width) if outline else 0,
        )

    def oval(self, x: int, y: int, w: int, h: int, color: str,
             outline: str = "", width: int = 1) -> None:
        self.canvas.create_oval(
            self.box(x), self.box(y), self.box(x + w), self.box(y + h),
            fill=color, outline=outline, width=self.box(width) if outline else 0,
        )

    def polygon(self, points: list[tuple[int, int]], color: str) -> None:
        coordinates = [self.box(value) for point in points for value in point]
        self.canvas.create_polygon(*coordinates, fill=color, outline="")

    def text(self, x: int, y: int, value: str, size: int = 10,
             color: str = TEXT, anchor: str = "nw") -> None:
        self.canvas.create_text(
            self.box(x), self.box(y), text=value, fill=color, anchor=anchor,
            font=("Menlo", self.box(size), "bold"),
        )

    @staticmethod
    def road_x(boundary: int, y: int) -> int:
        far_x = (96, 116, 124, 144)[boundary]
        near_x = (12, 84, 156, 228)[boundary]
        return far_x + (near_x - far_x) * (y - 65) // 185

    @staticmethod
    def project(rect: RunnerRect, y_cp: int, lane: int,
                x_cp: int | None = None) -> tuple[int, int, int, int, int]:
        y = (y_cp + CP_SCALE // 2) // CP_SCALE
        scale = 25 + (y - 55) * 75 // (214 - 55)
        scale = max(25, min(110, scale))
        w = max(7, rect.w * scale // 100)
        h = max(7, rect.h * scale // 100)
        far_x = (106, 120, 134)[lane]
        near_x = (48, 120, 192)[lane] if x_cp is None else x_cp // CP_SCALE
        travel = max(0, min(185, y - 65))
        center = far_x + (near_x - far_x) * travel // 185
        return center - w // 2, rect.y, w, h, scale

    def draw_player(self) -> None:
        rect = self.lib.runner_game_player_rect(C.byref(self.game))
        jump = self.lib.runner_game_jump_height(C.byref(self.game))
        x, y = rect.x, rect.y + ((self.game.elapsed_ticks // 4) % 2 if self.game.mode == MODE_RUNNING and not jump else 0)
        target = self.lib.runner_game_lane_center(self.game.lane)
        lean = max(-4, min(4, (target - self.game.player_x) // 1400))
        accent = STAGE_ACCENTS[self.game.stage]
        body = GOLD if self.game.overload_ticks else BLUE
        if self.game.invincible_ticks and (self.game.invincible_ticks // 4) % 2:
            accent, body = TEXT, MUTED
        shadow_size = max(14, 30 - jump // 3)
        self.oval(x + 12 - shadow_size // 2, 247, shadow_size, 7, SHADOW)
        if lean or self.game.rush_ticks or self.game.overload_ticks:
            trail_x = x + (20 if lean >= 0 else -5)
            self.rectangle(trail_x, y + 12, 7, 18, GOLD if self.game.rush_ticks or self.game.overload_ticks else GRID)
        if self.game.overload_ticks:
            self.oval(x - 9, y - 12, 42, 56, "", GOLD, 2)
        if self.game.shield_ticks:
            self.oval(x - 7, y - 10, 38, 52, "", CYAN, 2)
        self.rectangle(x + 10 + lean, y - 4, 2, 5, accent)
        self.oval(x + 8 + lean, y - 7, 6, 5, GOLD)
        self.rectangle(x + lean, y, 24, 14, accent, TEXT if self.game.invincible_ticks else "", 1)
        self.rectangle(x + 3 + lean, y + 3, 18, 7, PANEL)
        self.rectangle(x + 6 + lean, y + 5, 4, 3, TEXT)
        self.rectangle(x + 15 + lean, y + 5, 4, 3, TEXT)
        self.rectangle(x + 3, y + 13, 18, 15, body)
        self.rectangle(x + 8, y + 17, 8, 5, GOLD)
        step = (self.game.elapsed_ticks // 3) % 2 if self.game.mode == MODE_RUNNING and not jump else 0
        self.rectangle(x - 2, y + 15 + step, 5, 10, accent)
        self.rectangle(x + 21, y + 16 - step, 5, 10, accent)
        leg_y = 27 if jump else 26 + (0 if step else 2)
        self.rectangle(x + 4, y + leg_y, 6, 8, accent)
        self.rectangle(x + 14, y + leg_y, 6, 8, accent)
        self.rectangle(x + 3, y + leg_y + 6, 8, 3, TEXT)
        self.rectangle(x + 13, y + leg_y + 6, 8, 3, TEXT)

    def draw_obstacle(self, index: int) -> None:
        obstacle = self.game.obstacles[index]
        rect = self.lib.runner_game_obstacle_rect(C.byref(obstacle))
        x, y, w, h, scale = self.project(rect, obstacle.y, obstacle.lane)
        self.rectangle(x - 1, y + h - 1, w + 2, max(2, h // 6), SHADOW)
        if obstacle.kind == 1:
            self.rectangle(x, y, w, h, "#A95A24")
            self.rectangle(x + 1, y + 1, w - 2, max(3, h // 3), GOLD)
            self.rectangle(x + 1, y + h // 2, w - 2, max(2, h // 5), TEXT)
            if scale > 65:
                self.text(x + w // 2, y - 11, "JUMP", 6, GOLD, "n")
        else:
            self.rectangle(x, y, w, h, DANGER_DARK)
            self.rectangle(x + 1, y + 1, w - 2, max(3, h // 5), DANGER)
            for stripe in range(3):
                self.rectangle(x + 2, y + h // 3 + stripe * max(3, h // 6), w - 4, 2, DANGER)

    @staticmethod
    def coin_opacity(y_cp: int) -> int:
        opacity = 255
        if y_cp < 70 * CP_SCALE:
            opacity = min(opacity, max(0, (y_cp - 55 * CP_SCALE) * 255 // (15 * CP_SCALE)))
        if y_cp > 250 * CP_SCALE:
            opacity = min(opacity, max(0, (280 * CP_SCALE - y_cp) * 255 // (30 * CP_SCALE)))
        return opacity

    @staticmethod
    def blend_color(foreground: str, background: str, opacity: int) -> str:
        fg = tuple(int(foreground.lstrip("#")[i:i + 2], 16) for i in (0, 2, 4))
        bg = tuple(int(background.lstrip("#")[i:i + 2], 16) for i in (0, 2, 4))
        rgb = tuple((f * opacity + b * (255 - opacity)) // 255 for f, b in zip(fg, bg))
        return "#" + "".join(f"{channel:02X}" for channel in rgb)

    def draw_coin(self, index: int) -> None:
        coin = self.game.coins[index]
        opacity = self.coin_opacity(coin.y)
        if opacity == 0:
            return
        rect = self.lib.runner_game_coin_rect(C.byref(coin))
        x, y, w, _h, _scale = self.project(rect, coin.y, coin.lane)
        spin = (self.game.elapsed_ticks + index * 3) % 16
        width = max(3, w * abs(8 - spin) // 8)
        x += (w - width) // 2
        road = STAGE_ROADS[self.game.stage]
        color = self.blend_color("#FF9F43" if coin.value > 1 else GOLD, road, opacity)
        outline = self.blend_color(TEXT, road, opacity)
        self.oval(x, y, width, w, color, outline, 1)
        if width > 6:
            inner = self.blend_color("#D97835" if coin.value > 1 else "#D99435", road, opacity)
            self.oval(x + 2, y + 3, width - 4, w - 6, inner)

    def draw_powerup(self, index: int) -> None:
        powerup = self.game.powerups[index]
        rect = self.lib.runner_game_powerup_rect(C.byref(powerup))
        x, y, w, h, scale = self.project(rect, powerup.y, powerup.lane)
        colors = ("", "#8FF0A4", "#FF9F43")
        labels = ("", "S", "R")
        self.oval(x - 2, y - 2, w + 4, h + 4, PANEL, colors[powerup.kind], 2)
        if scale > 40:
            self.text(x + w // 2, y + h // 2, labels[powerup.kind], max(5, w // 2), colors[powerup.kind], "center")

    def draw_objects(self, near: bool) -> None:
        objects = [(obstacle.y, "o", index) for index, obstacle in enumerate(self.game.obstacles) if obstacle.active]
        objects += [(coin.y, "c", index) for index, coin in enumerate(self.game.coins) if coin.active]
        objects += [(powerup.y, "p", index) for index, powerup in enumerate(self.game.powerups) if powerup.active]
        for y, kind, index in sorted(objects):
            if (y >= 214 * CP_SCALE) != near:
                continue
            if kind == "o":
                self.draw_obstacle(index)
            elif kind == "c":
                self.draw_coin(index)
            else:
                self.draw_powerup(index)

    def current_hint(self) -> tuple[str, str] | None:
        urgent_feedback = (self.game.hit_flash_ticks or self.game.power_flash_ticks or
                           self.game.mission_flash_ticks or self.game.boss_flash_ticks)
        if (self.game.mode != MODE_RUNNING or self.game.route_offer or
                self.game.boss_active or urgent_feedback):
            return None
        if self.game.energy >= 100 and not self.game.overload_ticks:
            return "X: OVERLOAD", GOLD
        if self.game.elapsed_ticks < 85:
            return "ARROWS: CHANGE LANE", CYAN
        if self.game.elapsed_ticks < 170:
            return "SPACE: JUMP LOW BARS", CYAN
        if self.game.elapsed_ticks < 250:
            return "COINS CHARGE OVERLOAD", CYAN
        return None

    def draw_hint(self) -> None:
        hint = self.current_hint()
        if hint is None:
            return
        text, color = hint
        self.rectangle(24, 124, 192, 30, PANEL, color, 2)
        self.text(120, 132, text, 8, color, "n")

    def draw_route(self) -> None:
        if not self.game.route_offer or self.game.mode != MODE_RUNNING:
            return
        self.rectangle(24, 104, 192, 88, PANEL, GOLD, 2)
        self.text(120, 111, "CHOOSE YOUR ROUTE", 11, GOLD, "n")
        self.text(120, 132, f"ARROWS: CHOOSE  {(self.game.route_ticks + 49) // 50}s", 7, TEXT, "n")
        choices = (("SAFE", 48), ("NORMAL", 120), ("RISK", 192))
        for lane, (label, x) in enumerate(choices):
            selected = self.game.lane == lane
            self.rectangle(x - 28, 153, 56, 25, GRID if selected else BG,
                           CYAN if selected else MUTED, 1)
            self.text(x, 159, label, 7, TEXT if selected else MUTED, "n")

    def draw_boss(self) -> None:
        if not self.game.boss_active:
            return
        if not self.game.boss_vulnerable:
            for lane in range(3):
                if not (self.game.boss_attack_mask & (1 << lane)):
                    continue
                self.polygon([(self.road_x(lane, 65), 65), (self.road_x(lane + 1, 65), 65),
                              (self.road_x(lane + 1, 250), 250), (self.road_x(lane, 250), 250)], DANGER_DARK)
        color = TEXT if self.game.boss_flash_ticks and (self.game.boss_flash_ticks // 4) % 2 else DANGER
        if self.game.boss_vulnerable:
            color = GOLD
        self.rectangle(95, 79, 50, 31, PANEL, color, 2)
        self.rectangle(102, 86, 36, 15, color)
        self.rectangle(108, 90, 7, 5, BG)
        self.rectangle(125, 90, 7, 5, BG)
        for index in range(3):
            self.rectangle(103 + index * 13, 70, 10, 4,
                           color if index < self.game.boss_hp else GRID)
        status = "X: OVERLOAD NOW!" if self.game.boss_vulnerable else "ARROWS: DODGE RED LANE"
        self.text(120, 116, status, 8, color, "n")

    def draw_overlay(self) -> None:
        if self.game.mode == MODE_RUNNING:
            return
        accent = STAGE_ACCENTS[self.game.stage]
        self.rectangle(13, 72, 214, 166, PANEL, accent, 2)
        if self.game.mode == MODE_TITLE:
            title, line1, line2, line3 = (
                "ROBO DASH", "2 ROUTES + 3-HIT BOSS", "X OVERLOAD / SPACE JUMP", "ARROWS MOVE / SPACE START",
            )
        elif self.game.mode == MODE_PAUSED:
            title, line1, line2, line3 = (
                "PAUSED", "TAKE A BREATH", "SPACE / P  RESUME", "DOWN  TITLE",
            )
        elif self.game.mode == MODE_FINISHED:
            title = "STAGE CLEAR!"
            line1 = f"SCORE {self.game.score}  COINS {self.game.coin_count}"
            line2 = "NEW BEST!" if self.game.new_high_score else "FINISH BONUS INCLUDED"
            line3 = "SPACE RETRY / DOWN TITLE"
        else:
            title = "RUN OVER"
            line1 = f"SCORE {self.game.score}  COINS {self.game.coin_count}"
            line2 = "NEW BEST!" if self.game.new_high_score else "ONE MORE RUN?"
            line3 = "SPACE RETRY / DOWN TITLE"
        self.text(120, 91, title, 16, accent, "n")
        self.text(120, 133, line1, 9, GOLD, "n")
        self.text(120, 169, line2, 8, TEXT, "n")
        self.text(120, 203, line3, 8, MUTED, "n")

    def draw_background(self) -> None:
        accent = STAGE_ACCENTS[self.game.stage]
        road = STAGE_ROADS[self.game.stage]
        self.oval(100, 47, 40, 40, GRID)
        for index, (x, w, h) in enumerate(((0, 28, 27), (25, 20, 40), (45, 35, 22), (174, 25, 32), (198, 18, 45), (216, 24, 25))):
            self.rectangle(x, 65 - h, w, h, PANEL)
            if index % 2:
                self.rectangle(x + 5, 70 - h, 3, 3, accent)
        star_y = (65, 111, 81, 143, 101, 58, 128, 87, 157, 119)
        background_offset = self.game.distance // 400
        for index, y in enumerate(star_y):
            x = 244 - ((background_offset + index * 27) % 260)
            size = 3 if index % 3 == 0 else 2
            self.rectangle(x, y, size, size, GRID if index % 2 else MUTED)
        self.polygon([(self.road_x(0, 65), 65), (self.road_x(0, 292), 292),
                      (self.road_x(3, 292), 292), (self.road_x(3, 65), 65)], road)
        for boundary in range(4):
            self.canvas.create_line(
                self.box(self.road_x(boundary, 65)), self.box(65),
                self.box(self.road_x(boundary, 292)), self.box(292),
                fill=accent if boundary in (0, 3) else GRID,
                width=self.box(2 if boundary in (0, 3) else 1),
            )
        for lane in range(3):
            for row in range(9):
                phase = (self.game.distance // CP_SCALE + row * 26 + lane * 7) % 227
                y = 65 + phase
                left, right = self.road_x(lane, y), self.road_x(lane + 1, y)
                width = max(2, (right - left) // 4)
                self.rectangle((left + right - width) // 2, y, width, 1 + phase // 100, GRID)
        if self.game.mode == MODE_RUNNING and self.game.stage > 0:
            for index in range(4 + self.game.stage * 2):
                y = 82 + ((self.game.distance // 180 + index * 41) % 180)
                x = 5 + (index * 37) % 230
                if self.road_x(0, y) < x < self.road_x(3, y):
                    continue
                self.rectangle(x, y, 1, 7 + self.game.stage * 3, MUTED)

    def draw_hud(self) -> None:
        accent = STAGE_ACCENTS[self.game.stage]
        elapsed = min(self.game.elapsed_ticks, DURATION_TICKS)
        remaining = (DURATION_TICKS - elapsed + 49) // 50
        self.text(12, 7, f"{self.game.score:05d}", 15, TEXT)
        self.text(13, 34, f"BEST {self.game.high_score:05d}", 8, GOLD)
        self.text(228, 9, f"LIFE {self.game.lives}/3", 10, DANGER if self.game.lives == 1 else accent, "ne")
        self.text(228, 34, f"STAGE {self.game.stage + 1}/3  {remaining}s", 8, TEXT, "ne")
        if self.game.multiplier > 1:
            self.text(13, 47, f"COMBO {self.game.combo}  x{self.game.multiplier}", 7, GOLD)
        active_power = ""
        if self.game.overload_ticks:
            active_power = f"OVERLOAD {(self.game.overload_ticks + 49) // 50}s"
        elif self.game.rush_ticks:
            active_power = f"RUSH {(self.game.rush_ticks + 49) // 50}s"
        elif self.game.shield_ticks:
            active_power = f"SHIELD {(self.game.shield_ticks + 49) // 50}s"
        if active_power:
            self.text(228, 49, active_power, 7, GOLD, "ne")
        self.rectangle(12, 60, 216, 3, GRID)
        self.rectangle(12, 60, max(1, 216 * elapsed // DURATION_TICKS), 3, accent)
        if self.game.stage_banner_ticks and self.game.mode == MODE_RUNNING:
            self.rectangle(50, 72, 140, 31, PANEL, accent, 1)
            self.text(120, 77, f"STAGE {self.game.stage + 1}", 10, accent, "n")
            subtitle = "SPACE JUMPS LOW BARS" if self.game.elapsed_ticks < 100 else f"CHECKPOINT +{self.game.stage * 100}"
            self.text(120, 91, subtitle, 6, TEXT, "n")
        if self.game.mission_flash_ticks and self.game.mode == MODE_RUNNING:
            self.text(120, 174, "MISSION +500", 12, GOLD, "n")
        elif self.game.power_flash_ticks and self.game.mode == MODE_RUNNING:
            names = ("", "SHIELD!", "RUSH x2!")
            self.text(120, 174, names[self.game.last_powerup], 12, GOLD, "n")
        elif self.game.hit_flash_ticks and self.game.mode == MODE_RUNNING:
            self.text(120, 178, "HIT! RECOVERING", 11, DANGER, "n")
        elif self.game.coin_flash_ticks and self.game.mode == MODE_RUNNING:
            self.text(120, 182, f"+{self.game.coin_feedback}  x{self.game.multiplier}", 11, GOLD, "n")
        ready = "  X READY" if self.game.energy >= 100 and not self.game.overload_ticks else ""
        self.text(12, 257, "ENERGY", 6, MUTED)
        self.text(228, 257, f"{self.game.energy}%{ready}", 7, GOLD if ready else TEXT, "ne")
        self.rectangle(12, 268, 216, 5, GRID)
        self.rectangle(12, 268, max(1, 216 * self.game.energy // 100), 5,
                       GOLD if self.game.energy >= 100 else CYAN)
        mission = "MISSION COMPLETE" if self.game.mission_complete else f"MISSION  JUMPS {min(self.game.jump_count, 10)}/10"
        self.text(120, 280, mission, 7, GOLD if self.game.mission_complete else TEXT, "n")

    def draw(self) -> None:
        self.canvas.delete("all")
        self.draw_background()
        self.draw_boss()
        self.draw_hud()
        self.draw_objects(near=False)
        self.draw_player()
        self.draw_objects(near=True)
        self.draw_route()
        self.draw_hint()
        self.text(120, 301, "ARROWS  SPACE JUMP  X OVERLOAD  P PAUSE", 6, MUTED, "n")
        if self.game.hit_flash_ticks:
            self.canvas.create_rectangle(2, 2, WIDTH * SCALE - 2, HEIGHT * SCALE - 2,
                                         outline=DANGER, width=self.box(3))
        self.draw_overlay()

    def capture(self, path: Path) -> None:
        from PIL import ImageGrab

        self.update()
        x, y = self.canvas.winfo_rootx(), self.canvas.winfo_rooty()
        width, height = self.canvas.winfo_width(), self.canvas.winfo_height()
        image = ImageGrab.grab(bbox=(x, y, x + width, y + height))
        path.parent.mkdir(parents=True, exist_ok=True)
        image.save(path)
        print(f"Simulator capture: {path}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rebuild", action="store_true", help="recompile the firmware C core")
    parser.add_argument("--check", action="store_true", help="compile and check without opening a window")
    parser.add_argument("--ui-smoke", action="store_true", help="open, render, and close one test window")
    parser.add_argument("--capture", type=Path, help="capture the ready hint and coin fades")
    parser.add_argument("--capture-route", type=Path, help="capture the naturally triggered route window")
    parser.add_argument("--capture-boss", type=Path, help="capture the naturally triggered boss warning")
    args = parser.parse_args()
    library = build_library(force=args.rebuild)
    lib = load_core(library)
    check_core(lib)
    if args.check:
        return
    if tk.TkVersion < 8.6:
        print(
            f"Tk {tk.TkVersion} cannot render this simulator reliably. "
            "Use simulator/play.command or a Python with Tk 8.6+ (Tk 9 recommended).",
            file=sys.stderr,
        )
        raise SystemExit(1)
    app = RunnerSimulator(lib)
    if args.ui_smoke or args.capture or args.capture_route or args.capture_boss:
        app.update()
        app.draw()
        app.send(INPUT_START)
        app.game.spawn_ticks = 100000
        app.game.powerup_spawn_ticks = 100000
        assert app.current_hint() == ("ARROWS: CHANGE LANE", CYAN)
        for _ in range(20):
            lib.runner_game_tick(C.byref(app.game))
        app.send(INPUT_LEFT)
        for _ in range(6):
            lib.runner_game_tick(C.byref(app.game))
        assert app.game.mode == MODE_RUNNING and app.game.lane == 0
        assert app.game.lives == 3
        app.send(INPUT_JUMP)
        for _ in range(17):
            lib.runner_game_tick(C.byref(app.game))
        assert lib.runner_game_jump_height(C.byref(app.game)) == 40
        app.game.obstacles[0].active = True
        app.game.obstacles[0].kind = 1
        app.game.obstacles[0].lane = 0
        app.game.obstacles[0].y = 174 * CP_SCALE
        app.game.powerups[0].active = True
        app.game.powerups[0].kind = 1
        app.game.powerups[0].lane = 1
        app.game.powerups[0].y = 130 * CP_SCALE
        for index, (lane, y) in enumerate(((0, 60), (1, 100), (2, 180), (0, 265))):
            app.game.coins[index].y = y * CP_SCALE
            app.game.coins[index].lane = lane
            app.game.coins[index].value = 1
            app.game.coins[index].active = True
        assert app.coin_opacity(55 * CP_SCALE) == 0
        assert app.coin_opacity(70 * CP_SCALE) == 255
        assert app.coin_opacity(250 * CP_SCALE) == 255
        assert app.coin_opacity(280 * CP_SCALE) == 0
        for y in (65, 94, 130, 174, 214, 250, 292):
            boundaries = [app.road_x(index, y) for index in range(4)]
            assert all(left < right for left, right in zip(boundaries, boundaries[1:]))
            for lane in range(3):
                left, right = boundaries[lane:lane + 2]
                width = max(2, (right - left) // 4)
                stripe_x = (left + right - width) // 2
                assert left < stripe_x and stripe_x + width < right
        app.game.coin_feedback = 20
        app.game.coin_flash_ticks = 20
        app.game.multiplier = 2
        app.game.energy = 100
        assert app.current_hint() == ("X: OVERLOAD", GOLD)
        app.draw()
        app.update()
        if args.capture:
            app.capture(args.capture)

        # Reach both major prompts through ordinary ticks, never by forcing their flags.
        lib.runner_game_start(C.byref(app.game), 123)
        app.game.spawn_ticks = 100000
        app.game.powerup_spawn_ticks = 100000
        while app.game.elapsed_ticks < 800:
            lib.runner_game_tick(C.byref(app.game))
        assert app.game.mode == MODE_RUNNING and app.game.elapsed_ticks == 800
        assert app.game.route_offer and app.game.route_ticks == 150
        assert app.current_hint() is None
        app.draw()
        app.update()
        if args.capture_route:
            app.capture(args.capture_route)
        while app.game.elapsed_ticks < 2400:
            app.game.spawn_ticks = 100000
            app.game.powerup_spawn_ticks = 100000
            lib.runner_game_tick(C.byref(app.game))
        assert app.game.mode == MODE_RUNNING and app.game.elapsed_ticks == 2400
        assert app.game.boss_active and app.game.boss_hp == 3
        assert not app.game.boss_vulnerable and app.current_hint() is None
        app.draw()
        app.update()
        if args.capture_boss:
            app.capture(args.capture_boss)
        attack_lane = next(lane for lane in range(3) if app.game.boss_attack_mask & (1 << lane))
        desired_lane = (attack_lane + 1) % 3
        while app.game.lane < desired_lane:
            app.send(INPUT_RIGHT)
        while app.game.lane > desired_lane:
            app.send(INPUT_LEFT)
        while not app.game.boss_vulnerable and app.game.mode == MODE_RUNNING:
            lib.runner_game_tick(C.byref(app.game))
        assert app.game.boss_vulnerable and app.game.lives == 3
        app.game.energy = 100
        app.send(INPUT_OVERLOAD)
        lib.runner_game_tick(C.byref(app.game))
        assert app.game.boss_hp == 2 and app.game.boss_overload_hit
        app.destroy()
        print("Simulator UI smoke: PASS (hints, fades, natural route and boss prompts, counterattack)")
        return
    print("Controls: arrows move, Space jump, X overload, P pause, Esc quit")
    app.mainloop()


if __name__ == "__main__":
    main()
