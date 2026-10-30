#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_fat.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

#define MOUNT_POINT       "/sdcard"
#define PIN_NUM_MISO      GPIO_NUM_19
#define PIN_NUM_MOSI      GPIO_NUM_23
#define PIN_NUM_CLK       GPIO_NUM_18
#define PIN_NUM_CS        GPIO_NUM_13
#define SHELL_PATH_MAX    256
#define SHELL_LINE_MAX    256
#define SHELL_ARG_MAX     8

static const char *TAG = "sd_shell";
static sdmmc_card_t *s_card;

/* Relative to the SD card root; empty string means /sdcard itself. */
static char s_current_dir[SHELL_PATH_MAX] = "";
static char s_prompt[SHELL_PATH_MAX + 16] = "sd:/> ";

static void update_prompt(void)
{
    if (s_current_dir[0] == '\0') {
        snprintf(s_prompt, sizeof(s_prompt), "sd:/> ");
    } else {
        snprintf(s_prompt, sizeof(s_prompt), "sd:/%s> ", s_current_dir);
    }
}

/*
 * Convert a user path into an absolute VFS path beneath /sdcard.
 * Absolute paths such as /docs/file.txt are interpreted from the SD root.
 * Attempts to walk above the root are clamped to the root.
 */
static esp_err_t resolve_path(const char *arg, char *out, size_t out_size)
{
    char work[SHELL_PATH_MAX * 2];
    char normalized[SHELL_PATH_MAX] = "";
    int written;

    if (arg == NULL || arg[0] == '\0') {
        written = snprintf(work, sizeof(work), "%s", s_current_dir);
    } else if (arg[0] == '/') {
        const size_t mount_len = strlen(MOUNT_POINT);
        if (strncmp(arg, MOUNT_POINT, mount_len) == 0 &&
            (arg[mount_len] == '\0' || arg[mount_len] == '/')) {
            arg += mount_len;
        }
        while (*arg == '/') {
            ++arg;
        }
        written = snprintf(work, sizeof(work), "%s", arg);
    } else if (s_current_dir[0] != '\0') {
        written = snprintf(work, sizeof(work), "%s/%s", s_current_dir, arg);
    } else {
        written = snprintf(work, sizeof(work), "%s", arg);
    }

    if (written < 0 || (size_t)written >= sizeof(work)) {
        return ESP_ERR_INVALID_SIZE;
    }

    char *save_ptr = NULL;
    for (char *part = strtok_r(work, "/", &save_ptr);
         part != NULL;
         part = strtok_r(NULL, "/", &save_ptr)) {
        if (strcmp(part, ".") == 0 || part[0] == '\0') {
            continue;
        }
        if (strcmp(part, "..") == 0) {
            char *last_slash = strrchr(normalized, '/');
            if (last_slash != NULL) {
                *last_slash = '\0';
            } else {
                normalized[0] = '\0';
            }
            continue;
        }

        const size_t used = strlen(normalized);
        const size_t part_len = strlen(part);
        const size_t extra = part_len + (used != 0 ? 1 : 0);
        if (used + extra >= sizeof(normalized)) {
            return ESP_ERR_INVALID_SIZE;
        }
        size_t append_at = used;
        if (used != 0) {
            normalized[append_at++] = '/';
        }
        memcpy(normalized + append_at, part, part_len + 1);
    }

    written = snprintf(out, out_size, "%s%s%s",
                       MOUNT_POINT,
                       normalized[0] != '\0' ? "/" : "",
                       normalized);
    if (written < 0 || (size_t)written >= out_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static void set_current_dir_from_path(const char *absolute_path)
{
    const char *relative = absolute_path + strlen(MOUNT_POINT);
    while (*relative == '/') {
        ++relative;
    }
    snprintf(s_current_dir, sizeof(s_current_dir), "%s", relative);
    update_prompt();
}

static esp_err_t mount_sd_card(void)
{
    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .read_only = true,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    /* Start conservatively; this matches the 4 MHz operation observed before. */
    host.max_freq_khz = 4000;

    const spi_bus_config_t bus_config = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    esp_err_t err = spi_bus_initialize(host.slot, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    ESP_LOGI(TAG, "Mounting SD card read-only at %s", MOUNT_POINT);
    err = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config,
                                  &mount_config, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(err));
        /* Mount failed; no filesystem is in use, so release the bus. */
        spi_bus_free(host.slot);
        return err;
    }

    ESP_LOGI(TAG, "SD card mounted read-only.");
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

static int cmd_pwd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("/%s\n", s_current_dir);
    return 0;
}

static int cmd_cd(int argc, char **argv)
{
    if (argc > 2) {
        printf("Usage: cd [directory]\n");
        return 1;
    }
    if (argc == 1) {
        s_current_dir[0] = '\0';
        update_prompt();
        return 0;
    }

    char path[SHELL_PATH_MAX];
    esp_err_t err = resolve_path(argv[1], path, sizeof(path));
    if (err != ESP_OK) {
        printf("cd: path is too long\n");
        return 1;
    }

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        printf("cd: not a directory: %s\n", argv[1]);
        return 1;
    }

    set_current_dir_from_path(path);
    return 0;
}

static int cmd_ls(int argc, char **argv)
{
    if (argc > 2) {
        printf("Usage: ls [path]\n");
        return 1;
    }

    char path[SHELL_PATH_MAX];
    if (resolve_path(argc == 2 ? argv[1] : NULL, path, sizeof(path)) != ESP_OK) {
        printf("ls: path is too long\n");
        return 1;
    }

    DIR *dir = opendir(path);
    if (dir == NULL) {
        printf("ls: cannot open '%s': %s\n",
               argc == 2 ? argv[1] : ".", strerror(errno));
        return 1;
    }

    bool found = false;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        found = true;

        char child_path[SHELL_PATH_MAX];
        int n = snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(child_path)) {
            printf("%s  [path too long]\n", entry->d_name);
            continue;
        }

        struct stat st;
        if (stat(child_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            printf("[DIR]  %s/\n", entry->d_name);
        } else if (stat(child_path, &st) == 0) {
            printf("%8lld  %s\n", (long long)st.st_size, entry->d_name);
        } else {
            printf("         %s\n", entry->d_name);
        }
    }
    closedir(dir);

    if (!found) {
        printf("(empty)\n");
    }
    return 0;
}

static int cmd_cat(int argc, char **argv)
{
    if (argc != 2) {
        printf("Usage: cat <file>\n");
        return 1;
    }

    char path[SHELL_PATH_MAX];
    if (resolve_path(argv[1], path, sizeof(path)) != ESP_OK) {
        printf("cat: path is too long\n");
        return 1;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        printf("cat: cannot open '%s': %s\n", argv[1], strerror(errno));
        return 1;
    }

    char buffer[128];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        fwrite(buffer, 1, count, stdout);
    }
    bool read_error = ferror(file);
    fclose(file);
    if (read_error) {
        printf("\ncat: error while reading '%s'\n", argv[1]);
        return 1;
    }
    printf("\n");
    return 0;
}

static const esp_console_cmd_t s_cmd_ls = {
    .command = "ls",
    .help = "List files and directories",
    .hint = "[path]",
    .func = &cmd_ls,
};

static const esp_console_cmd_t s_cmd_cd = {
    .command = "cd",
    .help = "Change directory; cd without arguments returns to SD root",
    .hint = "[directory]",
    .func = &cmd_cd,
};

static const esp_console_cmd_t s_cmd_pwd = {
    .command = "pwd",
    .help = "Print the current directory",
    .hint = NULL,
    .func = &cmd_pwd,
};

static const esp_console_cmd_t s_cmd_cat = {
    .command = "cat",
    .help = "Print a text file",
    .hint = "<file>",
    .func = &cmd_cat,
};

void app_main(void)
{
    ESP_LOGI(TAG, "Starting SD-card shell");

    if (mount_sd_card() != ESP_OK) {
        ESP_LOGE(TAG, "Shell not started because the SD card could not be mounted.");
        return;
    }

    update_prompt();

    ESP_ERROR_CHECK(esp_console_register_help_command());
    ESP_ERROR_CHECK(esp_console_cmd_register(&s_cmd_ls));
    ESP_ERROR_CHECK(esp_console_cmd_register(&s_cmd_cd));
    ESP_ERROR_CHECK(esp_console_cmd_register(&s_cmd_pwd));
    ESP_ERROR_CHECK(esp_console_cmd_register(&s_cmd_cat));

    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = s_prompt;
    repl_config.max_cmdline_length = SHELL_LINE_MAX;
    repl_config.max_cmdline_args = SHELL_ARG_MAX;

    esp_console_repl_t *repl = NULL;
    ESP_ERROR_CHECK(esp_console_new_repl_stdio(&repl_config, &repl));
    ESP_LOGI(TAG, "Ready. Type 'help' to list commands.");
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
