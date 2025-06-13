idf_component_register(SRCS "bpm_handler.c" "main.c" "ws_server.c" "spiffs.c" "display.c"
                    INCLUDE_DIRS "."
                    )

spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)