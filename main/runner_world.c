#include "runner_world.h"

#include <stdbool.h>
#include <stdint.h>

#define WORLD_W 240
#define WORLD_Y 65
#define WORLD_H 190

#define BG 0x080D1C
#define PANEL 0x111C33
#define GRID 0x24415E
#define CYAN 0x51F0DC
#define BLUE 0x3185D8
#define GOLD 0xFFD166
#define TEXT 0xF2FAFF
#define MUTED 0x89A5BD
#define DANGER 0xFF637D
#define DANGER_DARK 0x8D304B

static uint16_t s_pixels[WORLD_W * WORLD_H] __attribute__((aligned(4)));
static lv_obj_t *s_canvas;

static uint16_t color565(uint32_t rgb)
{
    uint32_t r = ((rgb >> 16) & 0xff) * 31 / 255;
    uint32_t g = ((rgb >> 8) & 0xff) * 63 / 255;
    uint32_t b = (rgb & 0xff) * 31 / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint32_t dim_color(uint32_t rgb, int opacity)
{
    if (opacity < 0) opacity = 0;
    if (opacity > 255) opacity = 255;
    uint32_t r = ((rgb >> 16) & 0xff) * opacity / 255;
    uint32_t g = ((rgb >> 8) & 0xff) * opacity / 255;
    uint32_t b = (rgb & 0xff) * opacity / 255;
    return (r << 16) | (g << 8) | b;
}

static void pixel(int x, int y, uint32_t color)
{
    if (x < 0 || x >= WORLD_W || y < WORLD_Y || y >= WORLD_Y + WORLD_H) return;
    s_pixels[(y - WORLD_Y) * WORLD_W + x] = color565(color);
}

static void fill_rect(int x, int y, int w, int h, uint32_t color)
{
    if (w <= 0 || h <= 0) return;
    int x1 = x < 0 ? 0 : x;
    int x2 = x + w > WORLD_W ? WORLD_W : x + w;
    int y1 = y < WORLD_Y ? WORLD_Y : y;
    int y2 = y + h > WORLD_Y + WORLD_H ? WORLD_Y + WORLD_H : y + h;
    if (x1 >= x2 || y1 >= y2) return;
    uint16_t native = color565(color);
    for (int row = y1; row < y2; ++row) {
        uint16_t *out = &s_pixels[(row - WORLD_Y) * WORLD_W + x1];
        for (int col = x1; col < x2; ++col) *out++ = native;
    }
}

static void rect_outline(int x, int y, int w, int h, int thickness, uint32_t color)
{
    fill_rect(x, y, w, thickness, color);
    fill_rect(x, y + h - thickness, w, thickness, color);
    fill_rect(x, y, thickness, h, color);
    fill_rect(x + w - thickness, y, thickness, h, color);
}

static void line(int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static void circle(int cx, int cy, int radius, uint32_t color)
{
    for (int dy = -radius; dy <= radius; ++dy) {
        int dx = radius;
        while (dx > 0 && dx * dx + dy * dy > radius * radius) --dx;
        fill_rect(cx - dx, cy + dy, dx * 2 + 1, 1, color);
    }
}

static int road_x(uint8_t boundary, int y)
{
    static const int far_x[4] = {96, 116, 124, 144};
    static const int near_x[4] = {12, 84, 156, 228};
    return far_x[boundary] + (near_x[boundary] - far_x[boundary]) * (y - 65) / 185;
}

static int perspective_scale(int32_t y_cp)
{
    int y = (y_cp + RUNNER_CP_SCALE / 2) / RUNNER_CP_SCALE;
    int scale = 25 + (y - RUNNER_OBSTACLE_SPAWN_Y) * 75 /
                (RUNNER_PLAYER_Y - RUNNER_OBSTACLE_SPAWN_Y);
    if (scale < 25) scale = 25;
    if (scale > 110) scale = 110;
    return scale;
}

static int lane_center(uint8_t lane, int32_t y_cp)
{
    static const int far_x[RUNNER_LANE_COUNT] = {106, 120, 134};
    int y = (y_cp + RUNNER_CP_SCALE / 2) / RUNNER_CP_SCALE;
    int travel = y - 65;
    if (travel < 0) travel = 0;
    if (travel > 185) travel = 185;
    int near_x = runner_game_lane_center(lane) / RUNNER_CP_SCALE;
    return far_x[lane] + (near_x - far_x[lane]) * travel / 185;
}

static uint32_t stage_accent(const runner_game_t *game)
{
    static const uint32_t colors[3] = {CYAN, 0xB99AFF, 0xFFAB5E};
    return colors[game->stage < 3 ? game->stage : 2];
}

static void draw_background(const runner_game_t *game)
{
    static const uint32_t roads[3] = {0x132A40, 0x211E43, 0x3A2535};
    fill_rect(0, WORLD_Y, WORLD_W, WORLD_H, roads[game->stage < 3 ? game->stage : 2]);
    static const int16_t star_x[6] = {9, 37, 94, 151, 202, 224};
    static const int16_t star_y[6] = {65, 111, 143, 58, 87, 119};
    for (size_t i = 0; i < 6; ++i) {
        fill_rect(star_x[i], star_y[i], i % 3 == 0 ? 3 : 2,
                  i % 3 == 0 ? 3 : 2, i % 2 ? GRID : MUTED);
    }
    for (uint8_t boundary = 0; boundary < 4; ++boundary) {
        bool edge = boundary == 0 || boundary == 3;
        line(road_x(boundary, 65), 65, road_x(boundary, 250), 250,
             edge ? CYAN : GRID);
        if (edge) line(road_x(boundary, 65) + 1, 65,
                       road_x(boundary, 250) + 1, 250, CYAN);
    }
    for (uint8_t lane = 0; lane < RUNNER_LANE_COUNT; ++lane) {
        for (uint8_t row = 0; row < 3; ++row) {
            int phase = (int)((game->distance / RUNNER_CP_SCALE +
                               (int32_t)row * 78 + (int32_t)lane * 7) % 227);
            int y = 65 + phase;
            int left = road_x(lane, y);
            int right = road_x(lane + 1, y);
            int width = (right - left) / 4;
            if (width < 2) width = 2;
            fill_rect((left + right - width) / 2, y, width, 1 + phase / 100, GRID);
        }
    }
}

static void draw_boss(const runner_game_t *game)
{
    if (!game->boss_active) return;
    if (!game->boss_vulnerable) {
        for (uint8_t lane = 0; lane < RUNNER_LANE_COUNT; ++lane) {
            if (!(game->boss_attack_mask & (1u << lane))) continue;
            // Fill the actual trapezoid between this lane's two boundaries.
            // A fixed-width line was wider than the far lanes and looked offset.
            for (int y = WORLD_Y; y <= RUNNER_GROUND_Y; ++y) {
                int left = road_x(lane, y) + 2;
                int right = road_x(lane + 1, y) - 2;
                if (right <= left) continue;
                fill_rect(left, y, right - left, 1, 0x75334A);
                pixel(left, y, DANGER);
                pixel(right - 1, y, DANGER);
            }
        }
    }
    uint32_t color = game->boss_vulnerable ? GOLD : DANGER;
    if (game->boss_flash_ticks && (game->boss_flash_ticks / 4) % 2) color = TEXT;
    fill_rect(95, 79, 50, 31, DANGER_DARK);
    rect_outline(95, 79, 50, 31, 2, color);
    fill_rect(107, 89, 7, 6, BG);
    fill_rect(126, 89, 7, 6, BG);
    for (size_t i = 0; i < 3; ++i) {
        if (i < game->boss_hp) fill_rect(103 + (int)i * 13, 70, 10, 4, DANGER);
    }
}

static void draw_obstacles(const runner_game_t *game)
{
    for (size_t i = 0; i < RUNNER_MAX_OBSTACLES; ++i) {
        const runner_obstacle_t *obstacle = &game->obstacles[i];
        if (!obstacle->active) continue;
        runner_rect_t rect = runner_game_obstacle_rect(obstacle);
        int scale = perspective_scale(obstacle->y);
        int w = rect.w * scale / 100;
        int h = rect.h * scale / 100;
        if (w < 7) w = 7;
        if (h < 7) h = 7;
        int x = lane_center(obstacle->lane, obstacle->y) - w / 2;
        bool barrier = obstacle->kind == RUNNER_OBSTACLE_BARRIER;
        fill_rect(x, rect.y, w, h, barrier ? 0xA95A24 : DANGER_DARK);
        fill_rect(x + 1, rect.y + 1, w - 2, h / 3 > 3 ? h / 3 : 3,
                  barrier ? GOLD : DANGER);
        fill_rect(x + 1, rect.y + h / 2, w - 2, h / 5 > 2 ? h / 5 : 2,
                  barrier ? TEXT : DANGER);
        if (scale > 45) {
            fill_rect(x + 2, rect.y, 3, 3, TEXT);
            fill_rect(x + w - 5, rect.y, 3, 3, TEXT);
        }
    }
}

static void draw_coins(const runner_game_t *game)
{
    for (size_t i = 0; i < RUNNER_MAX_COINS; ++i) {
        const runner_coin_t *coin = &game->coins[i];
        if (!coin->active) continue;
        runner_rect_t rect = runner_game_coin_rect(coin);
        int scale = perspective_scale(coin->y);
        int size = rect.w * scale / 100;
        if (size < 4) size = 4;
        int center = lane_center(coin->lane, coin->y);
        int opacity = 255;
        if (coin->y < 70 * RUNNER_CP_SCALE) {
            opacity = (coin->y - 55 * RUNNER_CP_SCALE) * 255 / (15 * RUNNER_CP_SCALE);
        }
        if (coin->y > 250 * RUNNER_CP_SCALE) {
            int fade = (280 * RUNNER_CP_SCALE - coin->y) * 255 / (30 * RUNNER_CP_SCALE);
            if (fade < opacity) opacity = fade;
        }
        uint32_t color = dim_color(coin->value > 1 ? 0xFF9F43 : GOLD, opacity);
        int radius = size / 2;
        circle(center, rect.y + radius, radius, color);
        if (radius >= 4) circle(center, rect.y + radius, radius > 1 ? radius - 1 : 1,
                                dim_color(TEXT, opacity));
        if (radius >= 4) circle(center, rect.y + radius, radius - 2, color);
    }
}

static void draw_powerups(const runner_game_t *game)
{
    static const uint32_t colors[] = {0, 0x8FF0A4, 0xFF9F43};
    for (size_t i = 0; i < RUNNER_MAX_POWERUPS; ++i) {
        const runner_powerup_t *powerup = &game->powerups[i];
        if (!powerup->active) continue;
        runner_rect_t rect = runner_game_powerup_rect(powerup);
        int size = rect.w * perspective_scale(powerup->y) / 100;
        if (size < 8) size = 8;
        int x = lane_center(powerup->lane, powerup->y) - size / 2;
        uint32_t color = colors[powerup->kind];
        circle(x + size / 2, rect.y + size / 2, size / 2, color);
        circle(x + size / 2, rect.y + size / 2, size / 2 > 2 ? size / 2 - 2 : 1, PANEL);
        if (powerup->kind == RUNNER_POWERUP_SHIELD) {
            rect_outline(x + size / 3, rect.y + size / 3, size / 3, size / 3, 1, color);
        } else {
            fill_rect(x + size / 3, rect.y + 2, 2, size - 4, color);
            fill_rect(x + size / 2, rect.y + 2, 2, size - 4, color);
        }
    }
}

static void draw_player(const runner_game_t *game)
{
    runner_rect_t rect = runner_game_player_rect(game);
    int32_t jump = runner_game_jump_height(game);
    int bob = game->mode == RUNNER_MODE_RUNNING && !jump ? (int)((game->elapsed_ticks / 4) % 2) : 0;
    int x = rect.x;
    int y = rect.y + bob - 4;
    uint32_t accent = stage_accent(game);
    if (game->invincible_ticks && (game->invincible_ticks / 4) % 2) accent = TEXT;
    uint32_t body = game->rush_ticks || game->overload_ticks ? GOLD : BLUE;
    // Keep the head centered over the body. The previous lane-dependent head
    // offset made the robot look stretched while changing lanes.
    fill_rect(x + 13, y, 2, 4, CYAN);
    fill_rect(x + 11, y, 6, 3, GOLD);
    fill_rect(x + 1, y + 4, 26, 15, accent);
    fill_rect(x + 4, y + 8, 20, 7, PANEL);
    fill_rect(x + 7, y + 10, 4, 3, TEXT);
    fill_rect(x + 17, y + 10, 4, 3, TEXT);
    fill_rect(x + 4, y + 19, 20, 14, body);
    fill_rect(x + 9, y + 23, 10, 5, GOLD);
    fill_rect(x, y + 21, 4, 9, CYAN);
    fill_rect(x + 24, y + 21, 4, 9, CYAN);
    int step = game->mode == RUNNER_MODE_RUNNING && !jump
             ? (int)((game->elapsed_ticks / 3) % 2) : 0;
    fill_rect(x + 5, y + (jump ? 29 : (step ? 31 : 33)), 7, 6, CYAN);
    fill_rect(x + 16, y + (jump ? 29 : (step ? 33 : 31)), 7, 6, CYAN);
    if (game->shield_ticks || game->overload_ticks) {
        rect_outline(x - 1, y - 1, 30, 42, 2, game->shield_ticks ? CYAN : GOLD);
    }
}

lv_obj_t *runner_world_create(lv_obj_t *parent)
{
    if (s_canvas) return s_canvas;
    s_canvas = lv_canvas_create(parent);
    if (!s_canvas) return NULL;
    lv_canvas_set_buffer(s_canvas, s_pixels, WORLD_W, WORLD_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas, 0, WORLD_Y);
    lv_obj_set_size(s_canvas, WORLD_W, WORLD_H);
    lv_obj_remove_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE);
    return s_canvas;
}

esp_err_t runner_world_render(const runner_game_t *game)
{
    if (!game || !s_canvas) return ESP_ERR_INVALID_STATE;
    draw_background(game);
    draw_boss(game);
    draw_obstacles(game);
    draw_coins(game);
    draw_powerups(game);
    draw_player(game);
    lv_obj_invalidate(s_canvas);
    return ESP_OK;
}
