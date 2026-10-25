idf_component_register(
    SRCS "main.c"
         "runner_game.c"
         "runner_world.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer nvs_flash esp_lcd
)
