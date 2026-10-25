#include "runner_game.h"

#include <string.h>

#define BASE_SPEED 6500
#define MAX_SPEED 14500
#define COIN_SIZE 12
#define POWERUP_SIZE 16
#define LANE_SHIFT_PER_TICK 1400
#define INVINCIBLE_TICKS 75
#define CHECKPOINT_TICKS 1000
#define ROUTE_FIRST_TICK 800
#define ROUTE_SECOND_TICK 1700
#define ROUTE_WINDOW_TICKS 150
#define BOSS_CYCLE_TICKS 200
#define BOSS_ATTACK_TICK 100
#define BOSS_OPEN_TICK 120

typedef enum {
    PATTERN_SINGLE = 0,
    PATTERN_JUMP,
    PATTERN_DOUBLE,
    PATTERN_RISK,
    PATTERN_ARC,
} pattern_t;

int32_t runner_game_lane_center(uint8_t lane)
{
    if (lane >= RUNNER_LANE_COUNT) lane = RUNNER_LANE_COUNT - 1;
    return (48 + lane * 72) * RUNNER_CP_SCALE;
}

int32_t runner_game_jump_height(const runner_game_t *game)
{
    if (!game->jump_ticks) return 0;
    int32_t elapsed = RUNNER_JUMP_TICKS - game->jump_ticks;
    return 4 * 40 * elapsed * (RUNNER_JUMP_TICKS - elapsed) /
           (RUNNER_JUMP_TICKS * RUNNER_JUMP_TICKS);
}

static runner_rect_t player_rect_cp(const runner_game_t *game)
{
    return (runner_rect_t){
        .x = game->player_x - 12 * RUNNER_CP_SCALE,
        .y = (RUNNER_PLAYER_Y - runner_game_jump_height(game)) * RUNNER_CP_SCALE,
        .w = 24 * RUNNER_CP_SCALE,
        .h = 36 * RUNNER_CP_SCALE,
    };
}

static runner_rect_t player_hitbox_cp(const runner_game_t *game)
{
    runner_rect_t box = player_rect_cp(game);
    box.x += 6 * RUNNER_CP_SCALE;
    box.y += 6 * RUNNER_CP_SCALE;
    box.w -= 12 * RUNNER_CP_SCALE;
    box.h -= 12 * RUNNER_CP_SCALE;
    return box;
}

static runner_rect_t obstacle_rect_cp(const runner_obstacle_t *obstacle)
{
    if (obstacle->kind == RUNNER_OBSTACLE_BARRIER) {
        return (runner_rect_t){
            .x = runner_game_lane_center(obstacle->lane) - 13 * RUNNER_CP_SCALE,
            .y = obstacle->y + 16 * RUNNER_CP_SCALE,
            .w = 26 * RUNNER_CP_SCALE,
            .h = 20 * RUNNER_CP_SCALE,
        };
    }
    return (runner_rect_t){
        .x = runner_game_lane_center(obstacle->lane) - 12 * RUNNER_CP_SCALE,
        .y = obstacle->y - 26 * RUNNER_CP_SCALE,
        .w = 24 * RUNNER_CP_SCALE,
        .h = 62 * RUNNER_CP_SCALE,
    };
}

static runner_rect_t coin_rect_cp(const runner_coin_t *coin)
{
    return (runner_rect_t){
        .x = runner_game_lane_center(coin->lane) - (COIN_SIZE / 2) * RUNNER_CP_SCALE,
        .y = coin->y - coin->height * RUNNER_CP_SCALE,
        .w = COIN_SIZE * RUNNER_CP_SCALE,
        .h = COIN_SIZE * RUNNER_CP_SCALE,
    };
}

static bool overlaps(runner_rect_t a, runner_rect_t b)
{
    return a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}

static uint32_t next_random(runner_game_t *game)
{
    uint32_t value = game->rng_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    game->rng_state = value;
    return value;
}

static void clear_world(runner_game_t *game)
{
    uint32_t high_score = game->high_score;
    memset(game, 0, sizeof(*game));
    game->high_score = high_score;
    game->lane = 1;
    game->safe_lane = 1;
    game->lives = RUNNER_START_LIVES;
    game->multiplier = 1;
    game->player_x = runner_game_lane_center(1);
}

void runner_game_init(runner_game_t *game, uint32_t high_score)
{
    memset(game, 0, sizeof(*game));
    game->mode = RUNNER_MODE_TITLE;
    game->high_score = high_score;
    game->lane = 1;
    game->safe_lane = 1;
    game->lives = RUNNER_START_LIVES;
    game->multiplier = 1;
    game->player_x = runner_game_lane_center(1);
}

void runner_game_start(runner_game_t *game, uint32_t seed)
{
    clear_world(game);
    game->mode = RUNNER_MODE_RUNNING;
    game->rng_state = seed ? seed : 0x52A7C9E1U;
    game->spawn_ticks = 90;
    game->powerup_spawn_ticks = 240;
    game->stage_banner_ticks = 70;
    game->events |= RUNNER_EVENT_START;
}

static void show_title(runner_game_t *game)
{
    clear_world(game);
    game->mode = RUNNER_MODE_TITLE;
}

void runner_game_input(runner_game_t *game, runner_input_t input, uint32_t seed)
{
    switch (game->mode) {
    case RUNNER_MODE_TITLE:
        if (input == RUNNER_INPUT_START) runner_game_start(game, seed);
        break;
    case RUNNER_MODE_RUNNING:
        if (input == RUNNER_INPUT_LEFT && game->lane > 0) {
            game->lane--;
            game->events |= RUNNER_EVENT_MOVE;
        } else if (input == RUNNER_INPUT_RIGHT && game->lane + 1 < RUNNER_LANE_COUNT) {
            game->lane++;
            game->events |= RUNNER_EVENT_MOVE;
        } else if (input == RUNNER_INPUT_JUMP && game->jump_ticks == 0) {
            game->jump_ticks = RUNNER_JUMP_TICKS;
            game->events |= RUNNER_EVENT_JUMP;
        } else if (input == RUNNER_INPUT_OVERLOAD && game->energy >= RUNNER_ENERGY_MAX &&
                   game->overload_ticks == 0) {
            game->energy = 0;
            game->overload_ticks = RUNNER_OVERLOAD_TICKS;
            game->boss_overload_hit = false;
            game->events |= RUNNER_EVENT_OVERLOAD;
        } else if (input == RUNNER_INPUT_PAUSE) {
            game->mode = RUNNER_MODE_PAUSED;
        } else if (input == RUNNER_INPUT_EXIT) {
            show_title(game);
        }
        break;
    case RUNNER_MODE_PAUSED:
        if (input == RUNNER_INPUT_START || input == RUNNER_INPUT_PAUSE) {
            game->mode = RUNNER_MODE_RUNNING;
        } else if (input == RUNNER_INPUT_EXIT) {
            show_title(game);
        }
        break;
    case RUNNER_MODE_GAME_OVER:
    case RUNNER_MODE_FINISHED:
        if (input == RUNNER_INPUT_START) {
            runner_game_start(game, seed);
        } else if (input == RUNNER_INPUT_PAUSE || input == RUNNER_INPUT_EXIT) {
            show_title(game);
        }
        break;
    }
}

int32_t runner_game_speed(const runner_game_t *game)
{
    int64_t speed = BASE_SPEED + (int64_t)game->elapsed_ticks * 2;
    if (game->invincible_ticks > 35) speed -= 800;
    if (game->rush_ticks > 0) speed += 1800;
    if (game->overload_ticks > 0) speed += 2200;
    if (speed > MAX_SPEED) speed = MAX_SPEED;
    if (speed < 5500) speed = 5500;
    return (int32_t)speed;
}

static void spawn_obstacle(runner_game_t *game, uint8_t lane, uint8_t kind)
{
    for (size_t i = 0; i < RUNNER_MAX_OBSTACLES; i++) {
        if (game->obstacles[i].active) continue;
        game->obstacles[i] = (runner_obstacle_t){
            .y = RUNNER_OBSTACLE_SPAWN_Y * RUNNER_CP_SCALE,
            .lane = lane,
            .kind = kind,
            .active = true,
        };
        return;
    }
}

static void spawn_coin(runner_game_t *game, uint8_t lane, int index,
                       uint8_t value, uint8_t height)
{
    int spawned = 0;
    for (size_t i = 0; i < RUNNER_MAX_COINS; i++) {
        if (game->coins[i].active) continue;
        if (spawned++ != index) continue;
        game->coins[i] = (runner_coin_t){
            .y = (RUNNER_OBSTACLE_SPAWN_Y + index * 17) * RUNNER_CP_SCALE,
            .lane = lane,
            .value = value,
            .height = height,
            .active = true,
        };
        return;
    }
}

static void spawn_coin_line(runner_game_t *game, uint8_t lane, uint8_t value, bool arc)
{
    static const uint8_t heights[4] = {0, 10, 20, 10};
    for (int i = 0; i < 4; i++) {
        spawn_coin(game, lane, i, value, arc ? heights[i] : 0);
    }
}

static uint8_t next_pattern(runner_game_t *game)
{
    static const uint8_t opening[] = {
        PATTERN_JUMP, PATTERN_SINGLE, PATTERN_JUMP, PATTERN_RISK,
        PATTERN_DOUBLE, PATTERN_ARC, PATTERN_JUMP, PATTERN_SINGLE,
    };
    if (game->pattern_index < sizeof(opening)) return opening[game->pattern_index++];
    game->pattern_index++;
    return (uint8_t)(next_random(game) % 5);
}

static void spawn_row(runner_game_t *game)
{
    int direction = (int)(next_random(game) % 3) - 1;
    int safe = (int)game->safe_lane + direction;
    if (safe < 0) safe = 0;
    if (safe >= RUNNER_LANE_COUNT) safe = RUNNER_LANE_COUNT - 1;
    if (safe != game->safe_lane && game->safe_lane_hold == 0) {
        game->safe_lane_hold = 1;
        game->spawn_ticks = 14;
        return;
    }
    game->safe_lane_hold = 0;
    game->safe_lane = (uint8_t)safe;

    uint8_t pattern = next_pattern(game);
    if (game->recovery_rows > 0) {
        pattern = PATTERN_SINGLE;
        game->recovery_rows--;
    } else if (game->elapsed_ticks < 450 && pattern == PATTERN_DOUBLE) {
        pattern = PATTERN_SINGLE;
    }
    if (game->route_risk_rows > 0) {
        if (pattern == PATTERN_SINGLE || pattern == PATTERN_ARC) pattern = PATTERN_RISK;
        game->route_risk_rows--;
    }
    if (game->route_safe_rows > 0) {
        if (pattern == PATTERN_DOUBLE || pattern == PATTERN_RISK) pattern = PATTERN_SINGLE;
        game->route_safe_rows--;
    }
    uint8_t other_a = (uint8_t)((game->safe_lane + 1) % 3);
    uint8_t other_b = (uint8_t)((game->safe_lane + 2) % 3);
    switch (pattern) {
    case PATTERN_JUMP:
        spawn_obstacle(game, game->safe_lane, RUNNER_OBSTACLE_BARRIER);
        spawn_coin_line(game, game->safe_lane, 1, true);
        break;
    case PATTERN_DOUBLE:
        spawn_obstacle(game, other_a, RUNNER_OBSTACLE_BLOCK);
        spawn_obstacle(game, other_b, RUNNER_OBSTACLE_BLOCK);
        spawn_coin_line(game, game->safe_lane, 1, false);
        break;
    case PATTERN_RISK:
        spawn_obstacle(game, other_a, RUNNER_OBSTACLE_BARRIER);
        spawn_obstacle(game, other_b, RUNNER_OBSTACLE_BARRIER);
        spawn_coin_line(game, game->safe_lane, 1, false);
        spawn_coin_line(game, other_a, 3, true);
        break;
    case PATTERN_ARC:
        spawn_coin_line(game, game->safe_lane, 2, true);
        break;
    case PATTERN_SINGLE:
    default:
        spawn_obstacle(game, other_a, RUNNER_OBSTACLE_BLOCK);
        spawn_coin_line(game, game->safe_lane, 1, false);
        break;
    }
    if (game->elapsed_ticks < 500) {
        game->spawn_ticks = 48 + (int32_t)(next_random(game) % 7);
    } else if (game->elapsed_ticks < 1500) {
        game->spawn_ticks = 40 + (int32_t)(next_random(game) % 7);
    } else {
        game->spawn_ticks = 34 + (int32_t)(next_random(game) % 7);
    }
    if (game->last_route == RUNNER_ROUTE_RISK && game->route_risk_rows > 0) game->spawn_ticks -= 8;
    if (game->last_route == RUNNER_ROUTE_SAFE && game->route_safe_rows > 0) game->spawn_ticks += 12;
}

static void spawn_powerup(runner_game_t *game)
{
    for (size_t i = 0; i < RUNNER_MAX_POWERUPS; i++) {
        if (game->powerups[i].active) continue;
        game->powerups[i] = (runner_powerup_t){
            .y = RUNNER_OBSTACLE_SPAWN_Y * RUNNER_CP_SCALE,
            .lane = game->safe_lane,
            .kind = (uint8_t)(RUNNER_POWERUP_SHIELD + next_random(game) % 2),
            .active = true,
        };
        break;
    }
    game->powerup_spawn_ticks = 400 + (int32_t)(next_random(game) % 201);
}

static void add_energy(runner_game_t *game, uint8_t amount)
{
    uint16_t energy = (uint16_t)game->energy + amount;
    game->energy = energy > RUNNER_ENERGY_MAX ? RUNNER_ENERGY_MAX : (uint8_t)energy;
}

static void update_route(runner_game_t *game)
{
    if (!game->route_offer) {
        if (game->elapsed_ticks == ROUTE_FIRST_TICK || game->elapsed_ticks == ROUTE_SECOND_TICK) {
            game->route_offer = true;
            game->route_ticks = ROUTE_WINDOW_TICKS;
            game->route_risk_rows = 0;
            game->route_safe_rows = 0;
            game->spawn_ticks = ROUTE_WINDOW_TICKS + 30;
            game->events |= RUNNER_EVENT_ROUTE;
        }
        return;
    }
    game->route_choice = game->lane;
    if (game->route_ticks > 0) game->route_ticks--;
    if (game->route_ticks > 0) return;
    game->route_offer = false;
    if (game->route_choice == 0) {
        game->last_route = RUNNER_ROUTE_SAFE;
        game->route_safe_rows = 8;
        if (game->lives < RUNNER_START_LIVES) game->lives++;
        add_energy(game, 20);
    } else if (game->route_choice == 2) {
        game->last_route = RUNNER_ROUTE_RISK;
        game->route_risk_rows = 8;
        game->bonus_score += 200;
        add_energy(game, 40);
    } else {
        game->last_route = RUNNER_ROUTE_NORMAL;
        game->bonus_score += 100;
        add_energy(game, 20);
    }
    game->events |= RUNNER_EVENT_ROUTE;
}

static void update_score(runner_game_t *game)
{
    game->score = game->elapsed_ticks / 5 + game->bonus_score;
}

static void update_high_score(runner_game_t *game)
{
    update_score(game);
    if (game->score > game->high_score) {
        game->high_score = game->score;
        game->new_high_score = true;
    }
}

static void finish_game(runner_game_t *game)
{
    game->mode = RUNNER_MODE_GAME_OVER;
    update_high_score(game);
}

static void finish_stage(runner_game_t *game)
{
    game->mode = RUNNER_MODE_FINISHED;
    game->bonus_score += game->lives * 100;
    game->events |= RUNNER_EVENT_WIN;
    update_high_score(game);
}

static void hit_player(runner_game_t *game, runner_obstacle_t *obstacle)
{
    obstacle->active = false;
    game->invincible_ticks = INVINCIBLE_TICKS;
    game->hit_flash_ticks = 25;
    if (game->shield_ticks > 0) {
        game->shield_ticks = 0;
        game->events |= RUNNER_EVENT_SHIELD_BREAK;
        return;
    }
    if (game->lives > 0) game->lives--;
    game->recovery_rows = 3;
    game->combo = 0;
    game->multiplier = 1;
    game->events |= RUNNER_EVENT_HIT;
    if (game->lives == 0) finish_game(game);
}

static void collect_powerup(runner_game_t *game, runner_powerup_t *powerup)
{
    powerup->active = false;
    game->last_powerup = powerup->kind;
    game->power_flash_ticks = 70;
    if (powerup->kind == RUNNER_POWERUP_SHIELD) game->shield_ticks = 600;
    if (powerup->kind == RUNNER_POWERUP_RUSH) game->rush_ticks = 200;
    game->events |= RUNNER_EVENT_POWERUP;
}

static void count_jump(runner_game_t *game, runner_obstacle_t *obstacle)
{
    obstacle->cleared = true;
    game->jump_count++;
    game->bonus_score += 25;
    game->events |= RUNNER_EVENT_JUMP;
    if (!game->mission_complete && game->jump_count >= RUNNER_MISSION_JUMPS) {
        game->mission_complete = true;
        game->bonus_score += 500;
        game->mission_flash_ticks = 100;
        game->events |= RUNNER_EVENT_MISSION;
    }
}

static void begin_boss_cycle(runner_game_t *game)
{
    uint8_t attack_lane = (uint8_t)(next_random(game) % RUNNER_LANE_COUNT);
    game->boss_phase_ticks = 0;
    game->boss_attack_mask = (uint8_t)(1u << attack_lane);
    game->boss_vulnerable = false;
    spawn_coin_line(game, (uint8_t)((attack_lane + 1) % RUNNER_LANE_COUNT), 4, false);
}

static void begin_boss(runner_game_t *game)
{
    game->boss_active = true;
    game->boss_hp = RUNNER_BOSS_HP;
    game->boss_flash_ticks = 0;
    game->spawn_ticks = 100000;
    game->powerup_spawn_ticks = 100000;
    game->route_offer = false;
    for (size_t i = 0; i < RUNNER_MAX_OBSTACLES; i++) game->obstacles[i].active = false;
    for (size_t i = 0; i < RUNNER_MAX_COINS; i++) game->coins[i].active = false;
    for (size_t i = 0; i < RUNNER_MAX_POWERUPS; i++) game->powerups[i].active = false;
    game->events |= RUNNER_EVENT_BOSS_START;
    begin_boss_cycle(game);
}

static void update_boss(runner_game_t *game)
{
    if (!game->boss_active) return;
    game->boss_phase_ticks++;
    if (game->boss_phase_ticks == BOSS_ATTACK_TICK) {
        game->events |= RUNNER_EVENT_BOSS_ATTACK;
        if ((game->boss_attack_mask & (1u << game->lane)) && game->overload_ticks == 0 &&
            game->invincible_ticks == 0) {
            runner_obstacle_t attack = {.active = true};
            hit_player(game, &attack);
            if (game->mode != RUNNER_MODE_RUNNING) return;
        }
    }
    if (game->boss_phase_ticks == BOSS_OPEN_TICK) {
        game->boss_vulnerable = true;
        game->events |= RUNNER_EVENT_BOSS_OPEN;
    }
    if (game->boss_vulnerable && game->overload_ticks > 0 && !game->boss_overload_hit) {
        game->boss_overload_hit = true;
        game->boss_vulnerable = false;
        game->boss_flash_ticks = 40;
        game->bonus_score += 250;
        if (game->boss_hp > 0) game->boss_hp--;
        game->events |= RUNNER_EVENT_BOSS_HIT;
        if (game->boss_hp == 0) {
            game->bonus_score += 500;
            finish_stage(game);
            return;
        }
        game->boss_phase_ticks = 160;
    }
    if (game->boss_phase_ticks >= BOSS_CYCLE_TICKS) begin_boss_cycle(game);
}

static void update_timers(runner_game_t *game)
{
    if (game->jump_ticks > 0) game->jump_ticks--;
    if (game->invincible_ticks > 0) game->invincible_ticks--;
    if (game->shield_ticks > 0) game->shield_ticks--;
    if (game->rush_ticks > 0) game->rush_ticks--;
    if (game->overload_ticks > 0) game->overload_ticks--;
    if (game->boss_flash_ticks > 0) game->boss_flash_ticks--;
    if (game->hit_flash_ticks > 0) game->hit_flash_ticks--;
    if (game->coin_flash_ticks > 0) game->coin_flash_ticks--;
    if (game->stage_banner_ticks > 0) game->stage_banner_ticks--;
    if (game->power_flash_ticks > 0) game->power_flash_ticks--;
    if (game->mission_flash_ticks > 0) game->mission_flash_ticks--;
}

void runner_game_tick(runner_game_t *game)
{
    if (game->mode != RUNNER_MODE_RUNNING) return;

    game->elapsed_ticks++;
    update_timers(game);
    if (game->elapsed_ticks == CHECKPOINT_TICKS || game->elapsed_ticks == CHECKPOINT_TICKS * 2) {
        game->stage = (uint8_t)(game->elapsed_ticks / CHECKPOINT_TICKS);
        if (game->lives < RUNNER_START_LIVES) game->lives++;
        game->bonus_score += game->stage * 100;
        game->stage_banner_ticks = 100;
        game->events |= RUNNER_EVENT_CHECKPOINT;
    }
    update_route(game);
    if (game->elapsed_ticks == RUNNER_BOSS_START_TICK) begin_boss(game);
    update_boss(game);
    if (game->mode != RUNNER_MODE_RUNNING) return;
    update_score(game);
    if (game->elapsed_ticks >= RUNNER_DURATION_TICKS) {
        game->boss_active = false;
        game->events |= RUNNER_EVENT_BOSS_ESCAPE;
        finish_game(game);
        return;
    }

    int32_t movement = runner_game_speed(game) / (1000 / RUNNER_TICK_MS);
    game->distance += movement;
    int32_t target_x = runner_game_lane_center(game->lane);
    if (game->player_x < target_x) {
        game->player_x += LANE_SHIFT_PER_TICK;
        if (game->player_x > target_x) game->player_x = target_x;
    } else if (game->player_x > target_x) {
        game->player_x -= LANE_SHIFT_PER_TICK;
        if (game->player_x < target_x) game->player_x = target_x;
    }

    if (!game->boss_active) {
        game->spawn_ticks--;
        if (game->spawn_ticks <= 0) spawn_row(game);
        game->powerup_spawn_ticks--;
        if (game->powerup_spawn_ticks <= 0) spawn_powerup(game);
    }
    for (size_t i = 0; i < RUNNER_MAX_OBSTACLES; i++) {
        runner_obstacle_t *obstacle = &game->obstacles[i];
        if (!obstacle->active) continue;
        obstacle->y += movement;
        if (obstacle->y > 270 * RUNNER_CP_SCALE) obstacle->active = false;
    }
    for (size_t i = 0; i < RUNNER_MAX_COINS; i++) {
        runner_coin_t *coin = &game->coins[i];
        if (!coin->active) continue;
        coin->y += movement;
        if (coin->y > 280 * RUNNER_CP_SCALE) coin->active = false;
    }
    for (size_t i = 0; i < RUNNER_MAX_POWERUPS; i++) {
        runner_powerup_t *powerup = &game->powerups[i];
        if (!powerup->active) continue;
        powerup->y += movement;
        if (powerup->y > 270 * RUNNER_CP_SCALE) powerup->active = false;
    }

    runner_rect_t visual_player = player_rect_cp(game);
    for (size_t i = 0; i < RUNNER_MAX_POWERUPS; i++) {
        runner_powerup_t *powerup = &game->powerups[i];
        if (!powerup->active) continue;
        runner_rect_t box = {
            .x = runner_game_lane_center(powerup->lane) - 10 * RUNNER_CP_SCALE,
            .y = powerup->y - 4 * RUNNER_CP_SCALE,
            .w = 20 * RUNNER_CP_SCALE,
            .h = 24 * RUNNER_CP_SCALE,
        };
        if (overlaps(visual_player, box)) collect_powerup(game, powerup);
    }
    for (size_t i = 0; i < RUNNER_MAX_COINS; i++) {
        runner_coin_t *coin = &game->coins[i];
        if (!coin->active) continue;
        runner_rect_t box = coin_rect_cp(coin);
        if (overlaps(visual_player, box)) {
            coin->active = false;
            game->coin_count++;
            if (game->combo < 200) game->combo++;
            game->multiplier = (uint8_t)(1 + game->combo / 5);
            if (game->multiplier > 5) game->multiplier = 5;
            uint8_t value = coin->value ? coin->value : 1;
            bool boosted = game->rush_ticks > 0 || game->overload_ticks > 0;
            game->coin_feedback = 10 * value * game->multiplier * (boosted ? 2 : 1);
            game->bonus_score += game->coin_feedback;
            add_energy(game, (uint8_t)(10 * value));
            game->coin_flash_ticks = 20;
            game->events |= RUNNER_EVENT_COIN;
        }
    }

    if (game->invincible_ticks == 0) {
        runner_rect_t hitbox = player_hitbox_cp(game);
        for (size_t i = 0; i < RUNNER_MAX_OBSTACLES; i++) {
            runner_obstacle_t *obstacle = &game->obstacles[i];
            if (!obstacle->active || obstacle->y <= 178 * RUNNER_CP_SCALE ||
                obstacle->y >= 240 * RUNNER_CP_SCALE) continue;
            runner_rect_t box = obstacle_rect_cp(obstacle);
            bool horizontal_overlap = hitbox.x < box.x + box.w && hitbox.x + hitbox.w > box.x;
            if (!horizontal_overlap) continue;
            if (obstacle->kind == RUNNER_OBSTACLE_BARRIER &&
                (obstacle->cleared || runner_game_jump_height(game) >= 18)) {
                if (!obstacle->cleared) count_jump(game, obstacle);
                continue;
            }
            if (game->rush_ticks > 0 || game->overload_ticks > 0) {
                obstacle->active = false;
                game->bonus_score += 25;
                game->events |= RUNNER_EVENT_SMASH;
                continue;
            }
            hit_player(game, obstacle);
            if (game->mode != RUNNER_MODE_RUNNING) return;
        }
    }
    update_score(game);
}

uint32_t runner_game_take_events(runner_game_t *game)
{
    uint32_t events = game->events;
    game->events = 0;
    return events;
}

static runner_rect_t scale_rect(runner_rect_t rect)
{
    rect.x /= RUNNER_CP_SCALE;
    rect.y /= RUNNER_CP_SCALE;
    rect.w /= RUNNER_CP_SCALE;
    rect.h /= RUNNER_CP_SCALE;
    return rect;
}

runner_rect_t runner_game_player_rect(const runner_game_t *game)
{
    return scale_rect(player_rect_cp(game));
}

runner_rect_t runner_game_obstacle_rect(const runner_obstacle_t *obstacle)
{
    return scale_rect(obstacle_rect_cp(obstacle));
}

runner_rect_t runner_game_coin_rect(const runner_coin_t *coin)
{
    return scale_rect(coin_rect_cp(coin));
}

runner_rect_t runner_game_powerup_rect(const runner_powerup_t *powerup)
{
    return scale_rect((runner_rect_t){
        .x = runner_game_lane_center(powerup->lane) - (POWERUP_SIZE / 2) * RUNNER_CP_SCALE,
        .y = powerup->y,
        .w = POWERUP_SIZE * RUNNER_CP_SCALE,
        .h = POWERUP_SIZE * RUNNER_CP_SCALE,
    });
}
