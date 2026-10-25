// Original offline robot-runner game for FoloToy AI Passport.
// The portable game model runs at 50 Hz; LVGL renders at 25 Hz. Button callbacks
// only enqueue events, and blocking PCM playback has its own worker task.
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "key_guard.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "runner_game.h"
#include "runner_world.h"

#define SCREEN_W 240
#define SCREEN_H 320
#define INPUT_QUEUE_DEPTH 12
#define AUDIO_QUEUE_DEPTH 4
#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
#define AUDIO_HZ 16000
#define JUMP_SAMPLES 1440
#define COIN_SAMPLES 1600
#define HIT_SAMPLES 3200

#define COLOR_BG 0x080D1C
#define COLOR_PANEL 0x111C33
#define COLOR_GRID 0x24415E
#define COLOR_CYAN 0x51F0DC
#define COLOR_BLUE 0x3185D8
#define COLOR_GOLD 0xFFD166
#define COLOR_TEXT 0xF2FAFF
#define COLOR_MUTED 0x89A5BD
#define COLOR_DANGER 0xFF637D
#define COLOR_DANGER_DARK 0x8D304B

typedef struct {
    bsp_btn_t button;
    bsp_btn_ev_t event;
} input_event_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *crate;
    lv_obj_t *drone;
    lv_obj_t *eye;
    lv_obj_t *rotor;
} obstacle_ui_t;

static const char *TAG = "robo_dash";
static runner_game_t s_game;
static runner_game_t s_logic_game;
static QueueHandle_t s_input_queue;
static QueueHandle_t s_audio_queue;
static QueueHandle_t s_render_queue;
static key_guard_t s_key_guard;
static volatile bool s_input_ready;
static nvs_handle_t s_nvs;
static bool s_nvs_ready;

static int16_t s_jump_pcm[JUMP_SAMPLES];
static int16_t s_coin_pcm[COIN_SAMPLES];
static int16_t s_hit_pcm[HIT_SAMPLES];

static lv_obj_t *s_screen;
static lv_obj_t *s_score_label;
static lv_obj_t *s_best_label;
static lv_obj_t *s_battery_label;
static lv_obj_t *s_goal_label;
static lv_obj_t *s_life_label;
static lv_obj_t *s_combo_label;
static lv_obj_t *s_power_label;
static lv_obj_t *s_mission_label;
static lv_obj_t *s_feedback_label;
static lv_obj_t *s_energy_label;
static lv_obj_t *s_energy_bar;
static lv_obj_t *s_progress;
static lv_obj_t *s_road;
static lv_obj_t *s_lane_lines[4];
static lv_obj_t *s_boss_attack[3];
static lv_obj_t *s_boss;
static lv_obj_t *s_boss_hp[3];
static lv_obj_t *s_boss_warning;
static lv_obj_t *s_route;
static lv_obj_t *s_route_subtitle;
static lv_obj_t *s_route_choices[3];
static lv_obj_t *s_stage_banner;
static lv_obj_t *s_stage_title;
static lv_obj_t *s_stage_subtitle;
static lv_obj_t *s_hint;
static lv_obj_t *s_hint_label;
static lv_obj_t *s_player;
static lv_obj_t *s_robot_head;
static lv_obj_t *s_robot_body;
static lv_obj_t *s_robot_eye;
static lv_obj_t *s_robot_legs[2];
static obstacle_ui_t s_obstacles[RUNNER_MAX_OBSTACLES];
static lv_obj_t *s_coins[RUNNER_MAX_COINS];
static lv_obj_t *s_powerups[RUNNER_MAX_POWERUPS];
static lv_obj_t *s_powerup_labels[RUNNER_MAX_POWERUPS];
static lv_obj_t *s_stars[10];
static lv_obj_t *s_ground_marks[27];
static lv_obj_t *s_overlay;
static lv_obj_t *s_overlay_title;
static lv_obj_t *s_overlay_line1;
static lv_obj_t *s_overlay_line2;
static lv_obj_t *s_overlay_line3;

static void set_label_text(lv_obj_t *label, const char *text)
{
    const char *current = lv_label_get_text(label);
    if (current && strcmp(current, text) == 0) return;
    lv_label_set_text(label, text);
}

static void set_label_fmt(lv_obj_t *label, const char *format, ...)
{
    char text[64];
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    set_label_text(label, text);
}

static void set_style_bg_color(lv_obj_t *object, lv_color_t value,
                               lv_style_selector_t selector)
{
    if (!lv_color_eq(lv_obj_get_style_bg_color(object, selector), value)) {
        lv_obj_set_style_bg_color(object, value, selector);
    }
}

static void set_style_border_color(lv_obj_t *object, lv_color_t value,
                                   lv_style_selector_t selector)
{
    if (!lv_color_eq(lv_obj_get_style_border_color(object, selector), value)) {
        lv_obj_set_style_border_color(object, value, selector);
    }
}

static void set_style_text_color(lv_obj_t *object, lv_color_t value,
                                 lv_style_selector_t selector)
{
    if (!lv_color_eq(lv_obj_get_style_text_color(object, selector), value)) {
        lv_obj_set_style_text_color(object, value, selector);
    }
}

static void set_style_border_width(lv_obj_t *object, int32_t width,
                                   lv_style_selector_t selector)
{
    if (lv_obj_get_style_border_width(object, selector) != width) {
        lv_obj_set_style_border_width(object, width, selector);
    }
}

static lv_obj_t *make_box(lv_obj_t *parent, int x, int y, int w, int h,
                          uint32_t color, int radius)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    set_style_bg_color(box, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, radius, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    // UI creation runs on app_main while the LVGL mutex is held. Yield per
    // object so the idle task can feed the watchdog on the real ESP32-C3.
    vTaskDelay(1);
    return box;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    set_label_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    set_style_text_color(label, lv_color_hex(color), 0);
    vTaskDelay(1);
    return label;
}

static void set_hidden(lv_obj_t *object, bool hidden)
{
    if (hidden) {
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
}

static void load_high_score(void)
{
    uint32_t high_score = 0;
    esp_err_t error = nvs_flash_init();
    if (error == ESP_OK) error = nvs_open("robo_dash", NVS_READWRITE, &s_nvs);
    if (error == ESP_OK) {
        s_nvs_ready = true;
        error = nvs_get_u32(s_nvs, "best", &high_score);
        if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "high-score read failed: %s", esp_err_to_name(error));
        }
    } else {
        ESP_LOGW(TAG, "NVS unavailable; high score lasts for this run: %s",
                 esp_err_to_name(error));
    }
    runner_game_init(&s_logic_game, high_score);
}

static void save_high_score(void)
{
    if (!s_nvs_ready) return;
    esp_err_t error = nvs_set_u32(s_nvs, "best", s_logic_game.high_score);
    if (error == ESP_OK) error = nvs_commit(s_nvs);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "high-score save failed: %s", esp_err_to_name(error));
    }
}

static void generate_audio(void)
{
    for (int i = 0; i < JUMP_SAMPLES; i++) {
        float progress = (float)i / JUMP_SAMPLES;
        float phase = 6.2831853f * (460.0f * i / AUDIO_HZ + 520.0f * progress * i / AUDIO_HZ);
        float envelope = (1.0f - progress) * (i < 80 ? (float)i / 80.0f : 1.0f);
        s_jump_pcm[i] = (int16_t)(sinf(phase) * envelope * 7200.0f);
    }
    for (int i = 0; i < COIN_SAMPLES; i++) {
        float progress = (float)i / COIN_SAMPLES;
        float hz = i < COIN_SAMPLES / 2 ? 880.0f : 1320.0f;
        float envelope = (1.0f - progress) * (i < 60 ? (float)i / 60.0f : 1.0f);
        s_coin_pcm[i] = (int16_t)(sinf(6.2831853f * hz * i / AUDIO_HZ) * envelope * 6800.0f);
    }
    uint32_t noise = 0x61C88647U;
    for (int i = 0; i < HIT_SAMPLES; i++) {
        float progress = (float)i / HIT_SAMPLES;
        noise = noise * 1664525U + 1013904223U;
        float random = (float)((int32_t)(noise >> 16) - 32768) / 32768.0f;
        float tone = sinf(6.2831853f * (190.0f - 90.0f * progress) * i / AUDIO_HZ);
        s_hit_pcm[i] = (int16_t)((0.65f * tone + 0.35f * random) *
                                 (1.0f - progress) * 8500.0f);
    }
}

static void audio_worker(void *argument)
{
    (void)argument;
    uint8_t sound;
    for (;;) {
        if (xQueueReceive(s_audio_queue, &sound, portMAX_DELAY) != pdTRUE) continue;
        const int16_t *pcm = NULL;
        size_t bytes = 0;
        if (sound == RUNNER_EVENT_MOVE) {
            pcm = s_jump_pcm;
            bytes = sizeof(s_jump_pcm);
        } else if (sound == RUNNER_EVENT_COIN) {
            pcm = s_coin_pcm;
            bytes = sizeof(s_coin_pcm);
        } else if (sound == RUNNER_EVENT_HIT) {
            pcm = s_hit_pcm;
            bytes = sizeof(s_hit_pcm);
        }
        if (!pcm) continue;
        esp_err_t error = bsp_audio_write(pcm, bytes);
        if (error != ESP_OK) ESP_LOGW(TAG, "audio playback failed: %s", esp_err_to_name(error));
    }
}

static bool audio_start(void)
{
    if (bsp_audio_init() != ESP_OK || bsp_audio_set_format(AUDIO_HZ, 16, 1) != ESP_OK) {
        ESP_LOGW(TAG, "audio unavailable; continuing silently");
        return false;
    }
    bsp_audio_set_volume(65);
    generate_audio();
    s_audio_queue = xQueueCreate(AUDIO_QUEUE_DEPTH, sizeof(uint8_t));
    if (!s_audio_queue) return false;
    if (xTaskCreate(audio_worker, "runner_audio", 3072, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(s_audio_queue);
        s_audio_queue = NULL;
        return false;
    }
    return true;
}

static void queue_sound(uint8_t sound)
{
    if (s_audio_queue) (void)xQueueSend(s_audio_queue, &sound, 0);
}

static void create_robot(void)
{
    s_player = make_box(s_screen, 108, RUNNER_PLAYER_Y, 28, 40, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_player, LV_OPA_TRANSP, 0);
    make_box(s_player, 12, 0, 2, 5, COLOR_CYAN, 0);
    make_box(s_player, 10, 0, 6, 4, COLOR_GOLD, 2);
    s_robot_head = make_box(s_player, 0, 4, 24, 14, COLOR_CYAN, 3);
    s_robot_eye = make_box(s_robot_head, 3, 3, 18, 7, COLOR_PANEL, 1);
    make_box(s_robot_eye, 3, 2, 4, 3, COLOR_TEXT, 0);
    make_box(s_robot_eye, 12, 2, 4, 3, COLOR_TEXT, 0);
    s_robot_body = make_box(s_player, 3, 17, 18, 15, COLOR_BLUE, 2);
    make_box(s_robot_body, 5, 4, 8, 5, COLOR_GOLD, 1);
    make_box(s_player, 0, 19, 4, 10, COLOR_CYAN, 1);
    make_box(s_player, 24, 19, 4, 10, COLOR_CYAN, 1);
    s_robot_legs[0] = make_box(s_player, 4, 32, 6, 7, COLOR_CYAN, 1);
    s_robot_legs[1] = make_box(s_player, 14, 32, 6, 7, COLOR_CYAN, 1);
}

static void create_obstacle(size_t index)
{
    obstacle_ui_t *ui = &s_obstacles[index];
    ui->root = make_box(s_screen, 260, 226, 22, 20, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(ui->root, LV_OPA_TRANSP, 0);
    ui->crate = make_box(ui->root, 0, 0, 22, 20, COLOR_DANGER_DARK, 2);
    make_box(ui->crate, 1, 1, 20, 6, COLOR_DANGER, 1);
    ui->drone = make_box(ui->crate, 1, 9, 20, 4, COLOR_GOLD, 0);
    ui->eye = make_box(ui->crate, 2, 0, 3, 3, COLOR_TEXT, 0);
    ui->rotor = make_box(ui->crate, 17, 0, 3, 3, COLOR_TEXT, 0);
    set_hidden(ui->root, true);
}

static int road_x(uint8_t boundary, int y)
{
    static const int far_x[4] = {96, 116, 124, 144};
    static const int near_x[4] = {12, 84, 156, 228};
    return far_x[boundary] + (near_x[boundary] - far_x[boundary]) * (y - 65) / 185;
}

static int perspective_center(uint8_t lane, int32_t near_x_cp, int32_t y_cp)
{
    static const int far_x[RUNNER_LANE_COUNT] = {106, 120, 134};
    int y = (y_cp + RUNNER_CP_SCALE / 2) / RUNNER_CP_SCALE;
    int travel = y - 65;
    if (travel < 0) travel = 0;
    if (travel > 185) travel = 185;
    int near_x = near_x_cp / RUNNER_CP_SCALE;
    return far_x[lane] + (near_x - far_x[lane]) * travel / 185;
}

static int perspective_lane_center(uint8_t lane, int32_t y_cp)
{
    return perspective_center(lane, runner_game_lane_center(lane), y_cp);
}

static void create_ui(void)
{
    s_screen = make_box(NULL, 0, 0, SCREEN_W, SCREEN_H, COLOR_BG, 0);
    if (!runner_world_create(s_screen)) ESP_LOGE(TAG, "playfield canvas creation failed");

    static const int16_t star_x[6] = {9, 37, 94, 151, 202, 244};
    static const int16_t star_y[6] = {65, 111, 143, 58, 87, 119};
    for (size_t i = 0; i < 6; i++) {
        s_stars[i] = make_box(s_screen, star_x[i], star_y[i], i % 3 == 0 ? 3 : 2,
                              i % 3 == 0 ? 3 : 2, i % 2 ? COLOR_GRID : COLOR_MUTED, 0);
    }

    s_score_label = make_label(s_screen, "00000", &lv_font_montserrat_20, COLOR_TEXT);
    lv_obj_set_pos(s_score_label, 12, 7);
    s_best_label = make_label(s_screen, "BEST 00000", &lv_font_montserrat_14, COLOR_GOLD);
    lv_obj_set_pos(s_best_label, 13, 33);
    s_battery_label = make_label(s_screen, "", &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_set_pos(s_battery_label, 190, 48);
    int soc = bsp_battery_soc();
    if (soc >= 0) set_label_fmt(s_battery_label, "%d%%", soc);
    s_life_label = make_label(s_screen, "LIFE 3/3", &lv_font_montserrat_14, COLOR_CYAN);
    lv_obj_align(s_life_label, LV_ALIGN_TOP_RIGHT, -12, 8);
    s_goal_label = make_label(s_screen, "STAGE 1/3 60s", &lv_font_montserrat_14, COLOR_TEXT);
    lv_obj_align(s_goal_label, LV_ALIGN_TOP_RIGHT, -12, 32);
    s_combo_label = make_label(s_screen, "", &lv_font_montserrat_14, COLOR_GOLD);
    lv_obj_set_pos(s_combo_label, 13, 46);
    s_power_label = make_label(s_screen, "", &lv_font_montserrat_14, COLOR_GOLD);
    lv_obj_align(s_power_label, LV_ALIGN_TOP_RIGHT, -12, 48);
    set_hidden(s_power_label, true);
    make_box(s_screen, 12, 60, 216, 3, COLOR_GRID, 0);
    s_progress = make_box(s_screen, 12, 60, 1, 3, COLOR_CYAN, 0);
    s_energy_label = make_label(s_screen, "ENERGY 0%", &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_set_pos(s_energy_label, 12, 255);
    make_box(s_screen, 12, 271, 216, 5, COLOR_GRID, 0);
    s_energy_bar = make_box(s_screen, 12, 271, 1, 5, COLOR_CYAN, 0);

    s_road = make_box(s_screen, 0, 65, SCREEN_W, 227, COLOR_PANEL, 0);
    static lv_point_precise_t lane_points[4][2];
    for (uint8_t boundary = 0; boundary < 4; boundary++) {
        lane_points[boundary][0] = (lv_point_precise_t){road_x(boundary, 65), 65};
        lane_points[boundary][1] = (lv_point_precise_t){road_x(boundary, 292), 292};
        s_lane_lines[boundary] = lv_line_create(s_screen);
        lv_line_set_points(s_lane_lines[boundary], lane_points[boundary], 2);
        bool edge = boundary == 0 || boundary == 3;
        lv_obj_set_style_line_color(s_lane_lines[boundary],
                                    lv_color_hex(edge ? COLOR_CYAN : COLOR_GRID), 0);
        lv_obj_set_style_line_width(s_lane_lines[boundary], edge ? 2 : 1, 0);
    }
    static lv_point_precise_t boss_points[3][2];
    for (uint8_t lane = 0; lane < 3; lane++) {
        boss_points[lane][0] = (lv_point_precise_t){perspective_lane_center(lane, 65 * RUNNER_CP_SCALE), 65};
        boss_points[lane][1] = (lv_point_precise_t){runner_game_lane_center(lane) / RUNNER_CP_SCALE, 250};
        s_boss_attack[lane] = lv_line_create(s_screen);
        lv_line_set_points(s_boss_attack[lane], boss_points[lane], 2);
        lv_obj_set_style_line_color(s_boss_attack[lane], lv_color_hex(COLOR_DANGER), 0);
        lv_obj_set_style_line_width(s_boss_attack[lane], 28, 0);
        lv_obj_set_style_line_opa(s_boss_attack[lane], LV_OPA_40, 0);
        set_hidden(s_boss_attack[lane], true);
    }
    for (size_t i = 0; i < 9; i++) {
        s_ground_marks[i] = make_box(s_screen, 100, 65 + (int)i * 24, 4, 1, COLOR_GRID, 0);
    }
    s_boss = make_box(s_screen, 95, 79, 50, 31, COLOR_DANGER_DARK, 4);
    set_style_border_width(s_boss, 2, 0);
    set_style_border_color(s_boss, lv_color_hex(COLOR_DANGER), 0);
    make_box(s_boss, 12, 10, 7, 6, COLOR_BG, 0);
    make_box(s_boss, 31, 10, 7, 6, COLOR_BG, 0);
    for (size_t i = 0; i < 3; i++) {
        s_boss_hp[i] = make_box(s_boss, 8 + (int)i * 13, -9, 10, 4, COLOR_DANGER, 0);
    }
    set_hidden(s_boss, true);
    s_boss_warning = make_label(s_screen, "", &lv_font_montserrat_14, COLOR_DANGER);
    lv_obj_align(s_boss_warning, LV_ALIGN_TOP_MID, 0, 116);
    set_hidden(s_boss_warning, true);

    create_robot();
    for (size_t i = 0; i < RUNNER_MAX_OBSTACLES; i++) create_obstacle(i);
    for (size_t i = 0; i < RUNNER_MAX_COINS; i++) {
        s_coins[i] = make_box(s_screen, 260, 184, 12, 12, COLOR_GOLD, LV_RADIUS_CIRCLE);
        set_style_border_width(s_coins[i], 1, 0);
        set_style_border_color(s_coins[i], lv_color_hex(COLOR_TEXT), 0);
        set_hidden(s_coins[i], true);
    }
    for (size_t i = 0; i < RUNNER_MAX_POWERUPS; i++) {
        s_powerups[i] = make_box(s_screen, 260, 160, 20, 20, COLOR_PANEL, LV_RADIUS_CIRCLE);
        set_style_border_width(s_powerups[i], 2, 0);
        s_powerup_labels[i] = make_label(s_powerups[i], "S", &lv_font_montserrat_14, COLOR_CYAN);
        lv_obj_center(s_powerup_labels[i]);
        set_hidden(s_powerups[i], true);
    }

    s_mission_label = make_label(s_screen, "MISSION JUMPS 0/10", &lv_font_montserrat_14, COLOR_TEXT);
    lv_obj_align(s_mission_label, LV_ALIGN_BOTTOM_MID, 0, -24);
    s_feedback_label = make_label(s_screen, "", &lv_font_montserrat_20, COLOR_GOLD);
    lv_obj_align(s_feedback_label, LV_ALIGN_TOP_MID, 0, 177);
    set_hidden(s_feedback_label, true);
    s_stage_banner = make_box(s_screen, 50, 72, 140, 34, COLOR_PANEL, 4);
    set_style_border_width(s_stage_banner, 1, 0);
    set_style_border_color(s_stage_banner, lv_color_hex(COLOR_CYAN), 0);
    s_stage_title = make_label(s_stage_banner, "STAGE 1", &lv_font_montserrat_14, COLOR_CYAN);
    lv_obj_align(s_stage_title, LV_ALIGN_TOP_MID, 0, 2);
    s_stage_subtitle = make_label(s_stage_banner, "OK JUMPS LOW BARS", &lv_font_montserrat_14, COLOR_TEXT);
    lv_obj_align(s_stage_subtitle, LV_ALIGN_TOP_MID, 0, 18);
    set_hidden(s_stage_banner, true);

    s_hint = make_box(s_screen, 24, 124, 192, 30, COLOR_PANEL, 4);
    set_style_border_width(s_hint, 2, 0);
    set_style_border_color(s_hint, lv_color_hex(COLOR_GOLD), 0);
    s_hint_label = make_label(s_hint, "", &lv_font_montserrat_14, COLOR_GOLD);
    lv_obj_center(s_hint_label);
    set_hidden(s_hint, true);

    s_route = make_box(s_screen, 24, 67, 192, 60, COLOR_PANEL, 5);
    set_style_border_width(s_route, 2, 0);
    set_style_border_color(s_route, lv_color_hex(COLOR_GOLD), 0);
    lv_obj_t *route_title = make_label(s_route, "CHOOSE YOUR ROUTE", &lv_font_montserrat_14, COLOR_GOLD);
    lv_obj_align(route_title, LV_ALIGN_TOP_MID, 0, 2);
    s_route_subtitle = make_label(s_route, "MOVE TO A LANE 3s", &lv_font_montserrat_14, COLOR_TEXT);
    lv_obj_align(s_route_subtitle, LV_ALIGN_TOP_MID, 0, 21);
    static const char *route_names[3] = {"SAFE", "NORMAL", "RISK"};
    for (size_t i = 0; i < 3; i++) {
        s_route_choices[i] = make_label(s_route, route_names[i], &lv_font_montserrat_14, COLOR_MUTED);
        lv_obj_align(s_route_choices[i], LV_ALIGN_TOP_LEFT, 8 + (int)i * 61, 41);
    }
    set_hidden(s_route, true);

    lv_obj_t *controls = make_label(s_screen, "UP/DOWN  OK JUMP  2X OK BOOST",
                                    &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_align(controls, LV_ALIGN_BOTTOM_MID, 0, -8);

    s_overlay = make_box(s_screen, 13, 72, 214, 166, COLOR_PANEL, 8);
    set_style_border_width(s_overlay, 2, 0);
    set_style_border_color(s_overlay, lv_color_hex(COLOR_CYAN), 0);
    s_overlay_title = make_label(s_overlay, "ROBO DASH", &lv_font_montserrat_20, COLOR_CYAN);
    lv_obj_align(s_overlay_title, LV_ALIGN_TOP_MID, 0, 16);
    s_overlay_line1 = make_label(s_overlay, "2 ROUTES + 3-HIT BOSS", &lv_font_montserrat_14, COLOR_GOLD);
    lv_obj_align(s_overlay_line1, LV_ALIGN_TOP_MID, 0, 58);
    s_overlay_line2 = make_label(s_overlay, "DOUBLE OK = OVERLOAD", &lv_font_montserrat_14, COLOR_TEXT);
    lv_obj_align(s_overlay_line2, LV_ALIGN_TOP_MID, 0, 94);
    s_overlay_line3 = make_label(s_overlay, "OK START / HOLD PAUSE", &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_align(s_overlay_line3, LV_ALIGN_TOP_MID, 0, 128);

    // The moving playfield is drawn in RGB565 strips by runner_world.c. Keep
    // its former LVGL objects hidden so they cannot trigger layout or redraw.
    set_hidden(s_road, true);
    set_hidden(s_player, true);
    set_hidden(s_boss, true);
    for (size_t i = 0; i < 6; ++i) set_hidden(s_stars[i], true);
    for (size_t i = 0; i < 4; ++i) set_hidden(s_lane_lines[i], true);
    for (size_t i = 0; i < 9; ++i) set_hidden(s_ground_marks[i], true);
    for (size_t i = 0; i < RUNNER_MAX_OBSTACLES; ++i) set_hidden(s_obstacles[i].root, true);
    for (size_t i = 0; i < RUNNER_MAX_COINS; ++i) set_hidden(s_coins[i], true);
    for (size_t i = 0; i < RUNNER_MAX_POWERUPS; ++i) set_hidden(s_powerups[i], true);
    for (size_t i = 0; i < 3; ++i) set_hidden(s_boss_attack[i], true);

    lv_screen_load(s_screen);
}

static uint32_t stage_accent(void)
{
    static const uint32_t colors[3] = {COLOR_CYAN, 0xB99AFF, 0xFFAB5E};
    return colors[s_game.stage < 3 ? s_game.stage : 2];
}

static void render_overlay(void)
{
    set_hidden(s_overlay, s_game.mode == RUNNER_MODE_RUNNING);
    set_style_border_color(s_overlay, lv_color_hex(stage_accent()), 0);
    set_style_text_color(s_overlay_title, lv_color_hex(stage_accent()), 0);
    switch (s_game.mode) {
    case RUNNER_MODE_TITLE:
        set_label_text(s_overlay_title, "ROBO DASH");
        set_label_text(s_overlay_line1, "2 ROUTES + 3-HIT BOSS");
        set_label_text(s_overlay_line2, "DOUBLE OK = OVERLOAD");
        set_label_text(s_overlay_line3, "OK START / HOLD PAUSE");
        break;
    case RUNNER_MODE_PAUSED:
        set_label_text(s_overlay_title, "PAUSED");
        set_label_text(s_overlay_line1, "TAKE A BREATH");
        set_label_text(s_overlay_line2, "OK  RESUME");
        set_label_text(s_overlay_line3, "DOWN  TITLE");
        break;
    case RUNNER_MODE_GAME_OVER:
        set_label_text(s_overlay_title,
                          s_game.elapsed_ticks >= RUNNER_DURATION_TICKS && s_game.boss_hp > 0 ?
                          "BOSS ESCAPED" : "RUN OVER");
        set_label_fmt(s_overlay_line1, "SCORE %lu", (unsigned long)s_game.score);
        set_label_fmt(s_overlay_line2, "COINS %lu  %s", (unsigned long)s_game.coin_count,
                              s_game.new_high_score ? "NEW BEST" : "ONE MORE?");
        set_label_text(s_overlay_line3, "OK RETRY / DOWN TITLE");
        break;
    case RUNNER_MODE_FINISHED:
        set_label_text(s_overlay_title, "STAGE CLEAR!");
        set_label_fmt(s_overlay_line1, "SCORE %lu", (unsigned long)s_game.score);
        set_label_fmt(s_overlay_line2, "COINS %lu  %s", (unsigned long)s_game.coin_count,
                              s_game.new_high_score ? "NEW BEST" : "NICE RUN");
        set_label_text(s_overlay_line3, "OK RETRY / DOWN TITLE");
        break;
    case RUNNER_MODE_RUNNING:
        break;
    }
}

static void render_ui(void)
{
    if (!bsp_lvgl_lock(40)) return;
    if (s_game.mode != RUNNER_MODE_RUNNING) {
        // The full-screen overlay covers the world. Do not redraw hidden
        // scenery and entities on title, pause or result screens.
        render_overlay();
        bsp_lvgl_unlock();
        return;
    }
    uint32_t accent = stage_accent();
    static const uint32_t road_colors[3] = {0x132A40, 0x211E43, 0x3A2535};
    uint8_t stage = s_game.stage < 3 ? s_game.stage : 2;
    set_style_bg_color(s_road, lv_color_hex(road_colors[stage]), 0);
    set_style_bg_color(s_progress, lv_color_hex(accent), 0);
    set_style_border_width(s_screen, s_game.hit_flash_ticks ? 3 : 0, 0);
    set_style_border_color(s_screen, lv_color_hex(COLOR_DANGER), 0);

    set_label_fmt(s_score_label, "%05lu", (unsigned long)s_game.score);
    set_label_fmt(s_best_label, "BEST %05lu", (unsigned long)s_game.high_score);
    set_label_fmt(s_life_label, "LIFE %u/3", s_game.lives);
    set_style_text_color(s_life_label,
                                lv_color_hex(s_game.lives == 1 ? COLOR_DANGER : accent), 0);
    uint32_t elapsed = s_game.elapsed_ticks < RUNNER_DURATION_TICKS
                     ? s_game.elapsed_ticks : RUNNER_DURATION_TICKS;
    uint32_t remaining = (RUNNER_DURATION_TICKS - elapsed + 49) / 50;
    set_label_fmt(s_goal_label, "STAGE %u/3 %lus", s_game.stage + 1,
                          (unsigned long)remaining);
    lv_obj_set_width(s_progress, elapsed ? 216 * elapsed / RUNNER_DURATION_TICKS : 1);
    set_hidden(s_combo_label, s_game.multiplier <= 1);
    set_label_fmt(s_combo_label, "COMBO %u x%u", s_game.combo, s_game.multiplier);
    uint16_t power_ticks = s_game.overload_ticks ? s_game.overload_ticks :
                           (s_game.rush_ticks ? s_game.rush_ticks : s_game.shield_ticks);
    set_hidden(s_power_label, power_ticks == 0);
    set_hidden(s_battery_label, power_ticks != 0);
    if (power_ticks) {
        const char *name = s_game.overload_ticks ? "OVERLOAD" :
                           (s_game.rush_ticks ? "RUSH" : "SHIELD");
        set_label_fmt(s_power_label, "%s %us", name, (power_ticks + 49) / 50);
    }
    bool energy_ready = s_game.energy >= RUNNER_ENERGY_MAX && s_game.overload_ticks == 0;
    set_label_fmt(s_energy_label, energy_ready ? "ENERGY %u%%  2X OK!" : "ENERGY %u%%",
                          s_game.energy);
    set_style_text_color(s_energy_label, lv_color_hex(energy_ready ? COLOR_GOLD : COLOR_MUTED), 0);
    lv_obj_set_width(s_energy_bar, s_game.energy ? 216 * s_game.energy / RUNNER_ENERGY_MAX : 1);
    set_style_bg_color(s_energy_bar, lv_color_hex(energy_ready ? COLOR_GOLD : COLOR_CYAN), 0);
    if (s_game.mission_complete) {
        set_label_text(s_mission_label, "MISSION COMPLETE +500");
        set_style_text_color(s_mission_label, lv_color_hex(COLOR_GOLD), 0);
    } else {
        uint32_t jumps = s_game.jump_count < RUNNER_MISSION_JUMPS ? s_game.jump_count : RUNNER_MISSION_JUMPS;
        set_label_fmt(s_mission_label, "MISSION JUMPS %lu/%u",
                              (unsigned long)jumps, RUNNER_MISSION_JUMPS);
        set_style_text_color(s_mission_label, lv_color_hex(COLOR_TEXT), 0);
    }

    // Gameplay messages share the bottom status line instead of covering the
    // road. Coin collection is already visible in the score and energy bar.
    set_hidden(s_stage_banner, true);
    set_hidden(s_feedback_label, true);
    set_hidden(s_boss_warning, true);
    set_hidden(s_hint, true);
    if (s_game.stage_banner_ticks > 0) {
        if (s_game.elapsed_ticks < 100) {
            set_label_fmt(s_mission_label, "STAGE %u  OK JUMPS LOW BARS", s_game.stage + 1);
        } else {
            set_label_fmt(s_mission_label, "CHECKPOINT +%u", s_game.stage * 100);
        }
        set_style_text_color(s_mission_label, lv_color_hex(accent), 0);
    }
    if (s_game.boss_flash_ticks > 0) {
        set_label_text(s_mission_label, "BOSS HIT +250");
        set_style_text_color(s_mission_label, lv_color_hex(COLOR_GOLD), 0);
    } else if (s_game.mission_flash_ticks > 0) {
        set_label_text(s_mission_label, "MISSION COMPLETE +500");
        set_style_text_color(s_mission_label, lv_color_hex(COLOR_GOLD), 0);
    } else if (s_game.power_flash_ticks > 0) {
        set_label_text(s_mission_label,
                       s_game.last_powerup == RUNNER_POWERUP_SHIELD ? "SHIELD READY" : "RUSH x2");
        set_style_text_color(s_mission_label, lv_color_hex(COLOR_GOLD), 0);
    } else if (s_game.hit_flash_ticks > 0) {
        set_label_text(s_mission_label, "HIT! RECOVERING");
        set_style_text_color(s_mission_label, lv_color_hex(COLOR_DANGER), 0);
    }

    set_hidden(s_route, !s_game.route_offer || s_game.mode != RUNNER_MODE_RUNNING);
    if (s_game.route_offer) {
        set_label_fmt(s_route_subtitle, "UP/DOWN: CHOOSE %us", (s_game.route_ticks + 49) / 50);
        for (size_t i = 0; i < 3; i++) {
            set_style_text_color(s_route_choices[i],
                                        lv_color_hex(s_game.lane == i ? COLOR_CYAN : COLOR_MUTED), 0);
        }
    }
    if (s_game.boss_active) {
        uint32_t boss_color = s_game.boss_vulnerable ? COLOR_GOLD : COLOR_DANGER;
        set_label_text(s_mission_label, s_game.boss_vulnerable ?
                       "2X OK: COUNTER" : "UP/DOWN: DODGE RED");
        set_style_text_color(s_mission_label, lv_color_hex(boss_color), 0);
    } else if (!s_game.route_offer && !s_game.hit_flash_ticks &&
               !s_game.power_flash_ticks && !s_game.mission_flash_ticks &&
               !s_game.boss_flash_ticks && !s_game.stage_banner_ticks) {
        if (s_game.elapsed_ticks < 85) {
            set_label_text(s_mission_label, "UP/DOWN: CHANGE LANE");
            set_style_text_color(s_mission_label, lv_color_hex(COLOR_CYAN), 0);
        } else if (s_game.elapsed_ticks < 170) {
            set_label_text(s_mission_label, "OK: JUMP LOW BARS");
            set_style_text_color(s_mission_label, lv_color_hex(COLOR_CYAN), 0);
        } else if (s_game.elapsed_ticks < 250) {
            set_label_text(s_mission_label, "COINS CHARGE OVERLOAD");
            set_style_text_color(s_mission_label, lv_color_hex(COLOR_CYAN), 0);
        }
    }

    esp_err_t world_error = runner_world_render(&s_game);
    if (world_error != ESP_OK) {
        ESP_LOGW(TAG, "playfield draw failed: %s", esp_err_to_name(world_error));
    }
    render_overlay();
    bsp_lvgl_unlock();
}

static void process_input(const input_event_t *input)
{
    runner_input_t action;
    if (input->event != BSP_BTN_PRESS && input->event != BSP_BTN_LONG &&
        input->event != BSP_BTN_DOUBLE) return;
    if (input->button == BSP_BTN_OK) {
        if (input->event == BSP_BTN_DOUBLE) {
            if (s_logic_game.mode != RUNNER_MODE_RUNNING) return;
            action = RUNNER_INPUT_OVERLOAD;
        } else if (input->event == BSP_BTN_LONG) {
            if (s_logic_game.mode != RUNNER_MODE_RUNNING) return;
            action = RUNNER_INPUT_PAUSE;
        } else if (s_logic_game.mode == RUNNER_MODE_RUNNING) {
            action = RUNNER_INPUT_JUMP;
        } else if (s_logic_game.mode == RUNNER_MODE_PAUSED) {
            action = RUNNER_INPUT_PAUSE;
        } else {
            action = RUNNER_INPUT_START;
        }
    } else if (input->button == BSP_BTN_UP && input->event == BSP_BTN_PRESS) {
        action = RUNNER_INPUT_LEFT;
    } else if (input->button == BSP_BTN_DOWN && input->event == BSP_BTN_PRESS) {
        action = s_logic_game.mode == RUNNER_MODE_PAUSED || s_logic_game.mode == RUNNER_MODE_GAME_OVER ||
                 s_logic_game.mode == RUNNER_MODE_FINISHED ? RUNNER_INPUT_EXIT : RUNNER_INPUT_RIGHT;
    } else {
        return;
    }
    runner_game_input(&s_logic_game, action, (uint32_t)esp_timer_get_time());
}

static void game_worker(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        input_event_t input;
        while (xQueueReceive(s_input_queue, &input, 0) == pdTRUE) process_input(&input);
        runner_game_tick(&s_logic_game);
        uint32_t events = runner_game_take_events(&s_logic_game);
        (void)xQueueOverwrite(s_render_queue, &s_logic_game);
        if (events & (RUNNER_EVENT_MOVE | RUNNER_EVENT_JUMP)) queue_sound(RUNNER_EVENT_MOVE);
        if (events & (RUNNER_EVENT_COIN | RUNNER_EVENT_CHECKPOINT | RUNNER_EVENT_POWERUP |
                      RUNNER_EVENT_MISSION | RUNNER_EVENT_SMASH | RUNNER_EVENT_ROUTE |
                      RUNNER_EVENT_OVERLOAD | RUNNER_EVENT_BOSS_OPEN | RUNNER_EVENT_BOSS_HIT)) {
            queue_sound(RUNNER_EVENT_COIN);
        }
        if (events & (RUNNER_EVENT_HIT | RUNNER_EVENT_SHIELD_BREAK | RUNNER_EVENT_BOSS_START |
                      RUNNER_EVENT_BOSS_ATTACK | RUNNER_EVENT_BOSS_ESCAPE)) {
            if (s_logic_game.new_high_score) save_high_score();
            queue_sound(RUNNER_EVENT_HIT);
        }
        if (events & RUNNER_EVENT_WIN) {
            if (s_logic_game.new_high_score) save_high_score();
            queue_sound(RUNNER_EVENT_COIN);
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(RUNNER_TICK_MS));
    }
}

static void render_worker(void *argument)
{
    (void)argument;
    for (;;) {
        // The single-slot queue always yields the newest complete game state.
        // Rendering never shares a mutable state with the 50 Hz game task.
        if (xQueueReceive(s_render_queue, &s_game, pdMS_TO_TICKS(100)) == pdTRUE) {
            render_ui();
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

static void on_key(bsp_btn_t button, bsp_btn_ev_t event, void *user)
{
    (void)user;
    if (key_guard_consume(&s_key_guard, event == BSP_BTN_RELEASE, esp_timer_get_time())) {
        return;
    }
    if (!s_input_ready || !s_input_queue) return;
    input_event_t input = {.button = button, .event = event};
    (void)xQueueSend(s_input_queue, &input, 0);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Robo Dash starting");
    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & ~(1u << ESP_SLEEP_WAKEUP_UNDEFINED)) {
        ESP_LOGI(TAG, "wake causes: 0x%02x", (unsigned)wake_causes);
    }
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
    }

    load_high_score();
    bsp_i2c_init();
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display init failed (MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(85);
    (void)bsp_battery_init();
    bool audio_ok = audio_start();

    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    s_render_queue = xQueueCreate(1, sizeof(runner_game_t));
    if (!s_input_queue || !s_render_queue) {
        ESP_LOGE(TAG, "game queue creation failed");
        return;
    }
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "LVGL lock timeout");
        return;
    }
    create_ui();
    bsp_lvgl_unlock();
    (void)xQueueOverwrite(s_render_queue, &s_logic_game);
    if (xTaskCreate(render_worker, "runner_render", 6144, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "render task creation failed");
        return;
    }
    if (xTaskCreate(game_worker, "runner_game", 6144, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "game task creation failed");
        return;
    }
    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "button init failed");
        return;
    }
    if (s_key_guard.armed) {
        key_guard_sync_released(&s_key_guard,
                                bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN);
    }
    s_input_ready = true;
    ESP_LOGI(TAG, "ready: OK jump, double OK overload, hold OK pause (audio=%d, nvs=%d)",
             audio_ok, s_nvs_ready);
}
