#include <assert.h>
#include <stddef.h>

#include "runner_game.h"

static void no_spawn(runner_game_t *game)
{
    game->spawn_ticks = 100000;
    game->powerup_spawn_ticks = 100000;
}

static int32_t nearest_obstacle_y(const runner_game_t *game)
{
    int32_t nearest_y = -1;
    for (size_t i = 0; i < RUNNER_MAX_OBSTACLES; i++) {
        if (game->obstacles[i].active && !game->obstacles[i].cleared &&
            game->obstacles[i].y < 240 * RUNNER_CP_SCALE &&
            game->obstacles[i].y > nearest_y) nearest_y = game->obstacles[i].y;
    }
    return nearest_y;
}

static uint8_t row_mask(const runner_game_t *game, int32_t y, uint8_t kind)
{
    uint8_t mask = 0;
    for (size_t i = 0; i < RUNNER_MAX_OBSTACLES; i++) {
        if (game->obstacles[i].active && game->obstacles[i].y == y &&
            game->obstacles[i].kind == kind) mask |= 1u << game->obstacles[i].lane;
    }
    return mask;
}

static void auto_steer(runner_game_t *game, uint32_t seed, int32_t reaction_y)
{
    if (game->boss_active) {
        if (game->energy >= RUNNER_ENERGY_MAX && game->boss_vulnerable) {
            runner_game_input(game, RUNNER_INPUT_OVERLOAD, seed);
        }
        uint8_t desired = game->lane;
        int32_t coin_y = -1;
        for (size_t i = 0; i < RUNNER_MAX_COINS; i++) {
            if (game->coins[i].active && game->coins[i].y < 240 * RUNNER_CP_SCALE &&
                game->coins[i].y > coin_y) {
                coin_y = game->coins[i].y;
                desired = game->coins[i].lane;
            }
        }
        if (game->boss_phase_ticks > 40 && game->boss_phase_ticks < 110 &&
            (game->boss_attack_mask & (1u << desired))) {
            for (uint8_t lane = 0; lane < 3; lane++) {
                if (!(game->boss_attack_mask & (1u << lane))) { desired = lane; break; }
            }
        }
        if (desired < game->lane) runner_game_input(game, RUNNER_INPUT_LEFT, seed);
        if (desired > game->lane) runner_game_input(game, RUNNER_INPUT_RIGHT, seed);
        return;
    }
    int32_t nearest_y = nearest_obstacle_y(game);
    if (nearest_y < reaction_y) return;
    if (game->energy >= RUNNER_ENERGY_MAX && nearest_y > 140 * RUNNER_CP_SCALE) {
        runner_game_input(game, RUNNER_INPUT_OVERLOAD, seed);
    }
    uint8_t blocks = row_mask(game, nearest_y, RUNNER_OBSTACLE_BLOCK);
    uint8_t barriers = row_mask(game, nearest_y, RUNNER_OBSTACLE_BARRIER);
    uint8_t desired = game->lane;
    for (size_t i = 0; i < RUNNER_MAX_COINS; i++) {
        if (game->coins[i].active && game->coins[i].y == nearest_y &&
            game->coins[i].value <= 1 && !(blocks & (1u << game->coins[i].lane))) {
            desired = game->coins[i].lane;
            break;
        }
    }
    if (blocks & (1u << desired)) {
        for (uint8_t lane = 0; lane < 3; lane++) {
            if (!(blocks & (1u << lane)) && !(barriers & (1u << lane))) {
                desired = lane;
                break;
            }
        }
    }
    if (blocks & (1u << desired)) {
        for (uint8_t lane = 0; lane < 3; lane++) {
            if (!(blocks & (1u << lane))) { desired = lane; break; }
        }
    }
    if (desired < game->lane) runner_game_input(game, RUNNER_INPUT_LEFT, seed);
    if (desired > game->lane) runner_game_input(game, RUNNER_INPUT_RIGHT, seed);
    int32_t jump_y = -1;
    for (size_t i = 0; i < RUNNER_MAX_OBSTACLES; i++) {
        const runner_obstacle_t *obstacle = &game->obstacles[i];
        if (obstacle->active && !obstacle->cleared && obstacle->kind == RUNNER_OBSTACLE_BARRIER &&
            obstacle->lane == game->lane && obstacle->y < 240 * RUNNER_CP_SCALE &&
            obstacle->y > jump_y) jump_y = obstacle->y;
    }
    if (jump_y >= 160 * RUNNER_CP_SCALE && game->jump_ticks == 0)
        runner_game_input(game, RUNNER_INPUT_JUMP, seed);
}

int main(void)
{
    runner_game_t game;
    runner_game_init(&game, 42);
    assert(game.mode == RUNNER_MODE_TITLE && game.high_score == 42);
    assert(game.lane == 1 && runner_game_player_rect(&game).x == 108);
    assert(game.lives == 3 && game.multiplier == 1 && game.jump_count == 0);

    runner_game_input(&game, RUNNER_INPUT_START, 123);
    assert(game.mode == RUNNER_MODE_RUNNING);
    assert(runner_game_take_events(&game) == RUNNER_EVENT_START);
    no_spawn(&game);
    runner_game_input(&game, RUNNER_INPUT_LEFT, 123);
    assert(game.lane == 0 && (runner_game_take_events(&game) & RUNNER_EVENT_MOVE));
    for (int i = 0; i < 6; i++) runner_game_tick(&game);
    assert(game.player_x == runner_game_lane_center(0));
    runner_game_input(&game, RUNNER_INPUT_RIGHT, 123);
    for (int i = 0; i < 6; i++) runner_game_tick(&game);
    assert(game.lane == 1 && game.player_x == runner_game_lane_center(1));

    runner_game_input(&game, RUNNER_INPUT_PAUSE, 123);
    uint32_t paused_score = game.score;
    int32_t paused_x = game.player_x;
    for (int i = 0; i < 100; i++) runner_game_tick(&game);
    assert(game.score == paused_score && game.player_x == paused_x);
    runner_game_input(&game, RUNNER_INPUT_START, 123);
    assert(game.mode == RUNNER_MODE_RUNNING);

    runner_game_start(&game, 123);
    no_spawn(&game);
    runner_game_input(&game, RUNNER_INPUT_JUMP, 123);
    assert(game.jump_ticks == RUNNER_JUMP_TICKS);
    assert(runner_game_take_events(&game) & RUNNER_EVENT_JUMP);
    for (int i = 0; i < 17; i++) runner_game_tick(&game);
    assert(runner_game_jump_height(&game) == 40);
    assert(runner_game_player_rect(&game).y == RUNNER_PLAYER_Y - 40);
    for (int i = 0; i < 17; i++) runner_game_tick(&game);
    assert(runner_game_jump_height(&game) == 0);

    runner_game_start(&game, 123);
    no_spawn(&game);
    game.obstacles[0] = (runner_obstacle_t){
        .y = 214 * RUNNER_CP_SCALE, .lane = 1, .kind = RUNNER_OBSTACLE_BARRIER, .active = true};
    runner_game_tick(&game);
    assert(game.lives == 2 && game.mode == RUNNER_MODE_RUNNING);
    assert(runner_game_take_events(&game) & RUNNER_EVENT_HIT);

    runner_game_start(&game, 123);
    no_spawn(&game);
    runner_game_input(&game, RUNNER_INPUT_JUMP, 123);
    for (int i = 0; i < 17; i++) runner_game_tick(&game);
    game.obstacles[0] = (runner_obstacle_t){
        .y = 190 * RUNNER_CP_SCALE, .lane = 1, .kind = RUNNER_OBSTACLE_BARRIER, .active = true};
    runner_game_tick(&game);
    assert(game.lives == 3 && game.jump_count == 1 && game.obstacles[0].cleared);
    assert(game.bonus_score == 25);

    game.obstacles[0] = (runner_obstacle_t){
        .y = 214 * RUNNER_CP_SCALE, .lane = 1, .kind = RUNNER_OBSTACLE_BLOCK, .active = true};
    runner_game_tick(&game);
    assert(game.lives == 2 && game.mode == RUNNER_MODE_RUNNING);

    runner_game_start(&game, 123);
    no_spawn(&game);
    game.coins[0] = (runner_coin_t){
        .y = 214 * RUNNER_CP_SCALE, .lane = 0, .value = 1, .active = true};
    runner_game_tick(&game);
    assert(game.coins[0].active && game.coin_count == 0);
    assert(runner_game_coin_rect(&game.coins[0]).x == runner_game_lane_center(0) / RUNNER_CP_SCALE - 6);

    runner_game_start(&game, 123);
    no_spawn(&game);
    game.coins[0] = (runner_coin_t){
        .y = 214 * RUNNER_CP_SCALE, .lane = 1, .value = 1, .active = true};
    runner_game_tick(&game);
    assert(!game.coins[0].active && game.coin_count == 1 && game.score >= 10);
    assert(runner_game_take_events(&game) & RUNNER_EVENT_COIN);

    runner_game_start(&game, 123);
    no_spawn(&game);
    game.coins[0] = (runner_coin_t){
        .y = 130 * RUNNER_CP_SCALE, .lane = 0, .value = 1, .active = true};
    int32_t coin_start_y = game.coins[0].y;
    for (int i = 0; i < 10; i++) runner_game_tick(&game);
    assert(game.coins[0].active && game.coins[0].y > coin_start_y);
    assert(game.coins[0].lane == 0 && game.coin_count == 0);
    assert(runner_game_coin_rect(&game.coins[0]).x == runner_game_lane_center(0) / RUNNER_CP_SCALE - 6);

    runner_game_start(&game, 123);
    no_spawn(&game);
    game.shield_ticks = 100;
    game.obstacles[0] = (runner_obstacle_t){
        .y = 214 * RUNNER_CP_SCALE, .lane = 1, .kind = RUNNER_OBSTACLE_BLOCK, .active = true};
    runner_game_tick(&game);
    assert(game.lives == 3 && game.shield_ticks == 0);
    assert(runner_game_take_events(&game) & RUNNER_EVENT_SHIELD_BREAK);

    runner_game_start(&game, 123);
    no_spawn(&game);
    game.rush_ticks = 100;
    game.obstacles[0] = (runner_obstacle_t){
        .y = 214 * RUNNER_CP_SCALE, .lane = 1, .kind = RUNNER_OBSTACLE_BLOCK, .active = true};
    runner_game_tick(&game);
    assert(game.lives == 3 && !game.obstacles[0].active && game.bonus_score == 25);
    assert(runner_game_take_events(&game) & RUNNER_EVENT_SMASH);

    runner_game_start(&game, 123);
    no_spawn(&game);
    game.jump_count = RUNNER_MISSION_JUMPS - 1;
    runner_game_input(&game, RUNNER_INPUT_JUMP, 123);
    for (int i = 0; i < 17; i++) runner_game_tick(&game);
    game.obstacles[0] = (runner_obstacle_t){
        .y = 190 * RUNNER_CP_SCALE, .lane = 1, .kind = RUNNER_OBSTACLE_BARRIER, .active = true};
    runner_game_tick(&game);
    assert(game.mission_complete && game.mission_flash_ticks > 0 && game.bonus_score == 525);
    assert(runner_game_take_events(&game) & RUNNER_EVENT_MISSION);

    runner_game_start(&game, 123);
    game.spawn_ticks = 1;
    no_spawn(&game);
    game.spawn_ticks = 1;
    for (int i = 0; i < 20 && !game.obstacles[0].active; i++) runner_game_tick(&game);
    assert(game.obstacles[0].active && game.obstacles[0].kind == RUNNER_OBSTACLE_BARRIER);

    runner_game_start(&game, 123);
    no_spawn(&game);
    for (int i = 0; i < 1000; i++) runner_game_tick(&game);
    assert(game.stage == 1 && game.stage_banner_ticks > 0);
    assert(runner_game_take_events(&game) & RUNNER_EVENT_CHECKPOINT);

    runner_game_start(&game, 123);
    no_spawn(&game);
    game.energy = RUNNER_ENERGY_MAX - 1;
    runner_game_input(&game, RUNNER_INPUT_OVERLOAD, 123);
    assert(game.overload_ticks == 0 && game.energy == RUNNER_ENERGY_MAX - 1);
    game.energy = RUNNER_ENERGY_MAX;
    runner_game_input(&game, RUNNER_INPUT_OVERLOAD, 123);
    assert(game.overload_ticks == RUNNER_OVERLOAD_TICKS && game.energy == 0);
    assert(runner_game_take_events(&game) & RUNNER_EVENT_OVERLOAD);

    runner_game_start(&game, 123);
    no_spawn(&game);
    game.elapsed_ticks = 799;
    game.lives = 2;
    runner_game_input(&game, RUNNER_INPUT_LEFT, 123);
    runner_game_tick(&game);
    assert(game.route_offer && game.route_ticks == 150);
    game.route_ticks = 1;
    runner_game_tick(&game);
    assert(!game.route_offer && game.last_route == RUNNER_ROUTE_SAFE);
    assert(game.route_safe_rows == 8 && game.lives == 3 && game.energy == 20);

    runner_game_start(&game, 123);
    no_spawn(&game);
    game.elapsed_ticks = 1699;
    runner_game_input(&game, RUNNER_INPUT_RIGHT, 123);
    runner_game_tick(&game);
    assert(game.route_offer);
    game.route_ticks = 1;
    runner_game_tick(&game);
    assert(game.last_route == RUNNER_ROUTE_RISK && game.route_risk_rows == 8);
    assert(game.bonus_score == 200 && game.energy == 40);

    runner_game_start(&game, 123);
    no_spawn(&game);
    game.elapsed_ticks = RUNNER_BOSS_START_TICK - 1;
    runner_game_tick(&game);
    assert(game.boss_active && game.boss_hp == RUNNER_BOSS_HP);
    assert(runner_game_take_events(&game) & RUNNER_EVENT_BOSS_START);

    runner_game_start(&game, 123);
    no_spawn(&game);
    game.boss_active = true;
    game.boss_hp = RUNNER_BOSS_HP;
    game.boss_phase_ticks = 99;
    game.boss_attack_mask = 1u << 1;
    runner_game_tick(&game);
    assert(game.lives == 2);
    assert(runner_game_take_events(&game) & RUNNER_EVENT_BOSS_ATTACK);

    runner_game_start(&game, 123);
    no_spawn(&game);
    game.boss_active = true;
    game.boss_vulnerable = true;
    game.boss_hp = RUNNER_BOSS_HP;
    game.energy = RUNNER_ENERGY_MAX;
    runner_game_input(&game, RUNNER_INPUT_OVERLOAD, 123);
    runner_game_tick(&game);
    assert(game.boss_hp == RUNNER_BOSS_HP - 1 && game.boss_overload_hit);
    assert(game.bonus_score == 250);
    assert(runner_game_take_events(&game) & RUNNER_EVENT_BOSS_HIT);

    runner_game_start(&game, 123);
    no_spawn(&game);
    game.boss_active = true;
    game.boss_vulnerable = true;
    game.boss_hp = 1;
    game.energy = RUNNER_ENERGY_MAX;
    runner_game_input(&game, RUNNER_INPUT_OVERLOAD, 123);
    runner_game_tick(&game);
    assert(game.mode == RUNNER_MODE_FINISHED && game.boss_hp == 0);
    assert(game.high_score == game.score && game.new_high_score);
    assert(runner_game_take_events(&game) & RUNNER_EVENT_WIN);

    runner_game_start(&game, 123);
    no_spawn(&game);
    game.boss_active = true;
    game.boss_hp = RUNNER_BOSS_HP;
    game.elapsed_ticks = RUNNER_DURATION_TICKS - 1;
    runner_game_tick(&game);
    assert(game.mode == RUNNER_MODE_GAME_OVER);
    assert(runner_game_take_events(&game) & RUNNER_EVENT_BOSS_ESCAPE);

    // A visible-route player must clear 100 authored/mixed stages without a hit.
    for (uint32_t seed = 1; seed <= 100; seed++) {
        runner_game_start(&game, seed);
        unsigned hits = 0;
        for (int tick = 0; tick < RUNNER_DURATION_TICKS && game.mode == RUNNER_MODE_RUNNING; tick++) {
            auto_steer(&game, seed, 90 * RUNNER_CP_SCALE);
            runner_game_tick(&game);
            if (runner_game_take_events(&game) & RUNNER_EVENT_HIT) hits++;
        }
        assert(game.mode == RUNNER_MODE_FINISHED);
        assert(hits == 0 && game.lives == 3);
    }
    return 0;
}
