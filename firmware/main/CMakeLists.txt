idf_component_register(SRCS "main.c" "init/SDcard-init.c" "init/Display.c"
                       PRIV_REQUIRES spi_flash
                       INCLUDE_DIRS "")
