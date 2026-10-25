#ifndef RUNNER_WORLD_H
#define RUNNER_WORLD_H

#include "esp_err.h"
#include "lvgl.h"
#include "runner_game.h"

// One LVGL-owned canvas avoids recursive layout of moving objects without
// bypassing LVGL's refresh pipeline or fighting its overlays for the panel.
lv_obj_t *runner_world_create(lv_obj_t *parent);
esp_err_t runner_world_render(const runner_game_t *game);

#endif
