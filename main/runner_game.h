#ifndef RUNNER_GAME_H
#define RUNNER_GAME_H

#include <stdbool.h>
#include <stdint.h>

#define RUNNER_TICK_MS 20
#define RUNNER_GROUND_Y 250
#define RUNNER_PLAYER_Y 214
#define RUNNER_LANE_COUNT 3
#define RUNNER_MAX_OBSTACLES 16
#define RUNNER_MAX_COINS 40
#define RUNNER_MAX_POWERUPS 4
#define RUNNER_CP_SCALE 100
#define RUNNER_DURATION_TICKS 3000
#define RUNNER_OBSTACLE_SPAWN_Y 55
#define RUNNER_START_LIVES 3
#define RUNNER_JUMP_TICKS 34
#define RUNNER_MISSION_JUMPS 10
#define RUNNER_ENERGY_MAX 100
#define RUNNER_OVERLOAD_TICKS 150
#define RUNNER_BOSS_START_TICK 2400
#define RUNNER_BOSS_HP 3

typedef enum {
    RUNNER_MODE_TITLE = 0,
    RUNNER_MODE_RUNNING,
    RUNNER_MODE_PAUSED,
    RUNNER_MODE_GAME_OVER,
    RUNNER_MODE_FINISHED,
} runner_mode_t;

typedef enum {
    RUNNER_INPUT_START = 0,
    RUNNER_INPUT_LEFT,
    RUNNER_INPUT_RIGHT,
    RUNNER_INPUT_PAUSE,
    RUNNER_INPUT_EXIT,
    RUNNER_INPUT_JUMP,
    RUNNER_INPUT_OVERLOAD,
} runner_input_t;

typedef enum {
    RUNNER_EVENT_START = 1 << 0,
    RUNNER_EVENT_MOVE = 1 << 1,
    RUNNER_EVENT_COIN = 1 << 2,
    RUNNER_EVENT_HIT = 1 << 3,
    RUNNER_EVENT_WIN = 1 << 4,
    RUNNER_EVENT_CHECKPOINT = 1 << 5,
    RUNNER_EVENT_JUMP = 1 << 6,
    RUNNER_EVENT_POWERUP = 1 << 7,
    RUNNER_EVENT_MISSION = 1 << 8,
    RUNNER_EVENT_SMASH = 1 << 9,
    RUNNER_EVENT_SHIELD_BREAK = 1 << 10,
    RUNNER_EVENT_ROUTE = 1 << 11,
    RUNNER_EVENT_OVERLOAD = 1 << 12,
    RUNNER_EVENT_BOSS_START = 1 << 13,
    RUNNER_EVENT_BOSS_OPEN = 1 << 14,
    RUNNER_EVENT_BOSS_HIT = 1 << 15,
    RUNNER_EVENT_BOSS_ATTACK = 1 << 16,
    RUNNER_EVENT_BOSS_ESCAPE = 1 << 17,
} runner_event_t;

typedef enum {
    RUNNER_OBSTACLE_BLOCK = 0,
    RUNNER_OBSTACLE_BARRIER,
} runner_obstacle_kind_t;

typedef enum {
    RUNNER_POWERUP_NONE = 0,
    RUNNER_POWERUP_SHIELD = 1,
    RUNNER_POWERUP_RUSH = 2,
} runner_powerup_kind_t;

typedef enum {
    RUNNER_ROUTE_NONE = 0,
    RUNNER_ROUTE_SAFE,
    RUNNER_ROUTE_NORMAL,
    RUNNER_ROUTE_RISK,
} runner_route_t;

typedef struct {
    int32_t y;
    uint8_t lane;
    uint8_t kind;
    bool active;
    bool cleared;
} runner_obstacle_t;

typedef struct {
    int32_t y;
    uint8_t lane;
    uint8_t value;
    uint8_t height;
    bool active;
} runner_coin_t;

typedef struct {
    int32_t y;
    uint8_t lane;
    uint8_t kind;
    bool active;
} runner_powerup_t;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} runner_rect_t;

typedef struct {
    runner_mode_t mode;
    int32_t player_x;
    uint8_t lane;
    uint8_t safe_lane;
    uint8_t safe_lane_hold;
    uint8_t lives;
    uint8_t combo;
    uint8_t multiplier;
    uint8_t stage;
    uint8_t recovery_rows;
    uint8_t invincible_ticks;
    uint8_t hit_flash_ticks;
    uint8_t coin_flash_ticks;
    uint8_t stage_banner_ticks;
    uint8_t jump_ticks;
    uint8_t last_powerup;
    uint8_t power_flash_ticks;
    uint8_t pattern_index;
    uint8_t mission_flash_ticks;
    bool mission_complete;
    uint8_t energy;
    uint8_t route_choice;
    uint8_t last_route;
    uint8_t route_risk_rows;
    uint8_t route_safe_rows;
    uint8_t boss_hp;
    uint8_t boss_attack_mask;
    bool route_offer;
    bool boss_active;
    bool boss_vulnerable;
    bool boss_overload_hit;
    uint16_t shield_ticks;
    uint16_t rush_ticks;
    uint16_t overload_ticks;
    uint16_t route_ticks;
    uint16_t boss_phase_ticks;
    uint16_t boss_flash_ticks;
    runner_obstacle_t obstacles[RUNNER_MAX_OBSTACLES];
    runner_coin_t coins[RUNNER_MAX_COINS];
    runner_powerup_t powerups[RUNNER_MAX_POWERUPS];
    uint32_t score;
    uint32_t high_score;
    uint32_t coin_count;
    uint32_t bonus_score;
    uint32_t coin_feedback;
    uint32_t elapsed_ticks;
    uint32_t jump_count;
    int32_t distance;
    int32_t spawn_ticks;
    int32_t powerup_spawn_ticks;
    uint32_t rng_state;
    uint32_t events;
    bool new_high_score;
} runner_game_t;

void runner_game_init(runner_game_t *game, uint32_t high_score);
void runner_game_start(runner_game_t *game, uint32_t seed);
void runner_game_input(runner_game_t *game, runner_input_t input, uint32_t seed);
void runner_game_tick(runner_game_t *game);
uint32_t runner_game_take_events(runner_game_t *game);
int32_t runner_game_speed(const runner_game_t *game);
int32_t runner_game_jump_height(const runner_game_t *game);
int32_t runner_game_lane_center(uint8_t lane);
runner_rect_t runner_game_player_rect(const runner_game_t *game);
runner_rect_t runner_game_obstacle_rect(const runner_obstacle_t *obstacle);
runner_rect_t runner_game_coin_rect(const runner_coin_t *coin);
runner_rect_t runner_game_powerup_rect(const runner_powerup_t *powerup);

#endif
