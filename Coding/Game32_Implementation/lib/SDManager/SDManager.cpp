#include "SDManager.h"
#include "BoardConfig.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include <sys/stat.h>
#include <dirent.h>

static const char* TAG = "SDManager";

bool SDManager::initialize() {
    if (!mountFileSystem()) {
        ESP_LOGE(TAG, "Failed to mount SD card");
        return false;
    }
    createDefaultDirectories();
    return true;
}

bool SDManager::mountFileSystem() {
    esp_err_t ret;
    
    // Configuration for the FAT partition
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 16 * 1024;

    ESP_LOGI(TAG, "Initializing SD card via SPI");
    
    // Configure the SPI bus using our central BoardConfig
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = SD_MOSI_GPIO;
    bus_cfg.miso_io_num = SD_MISO_GPIO;
    bus_cfg.sclk_io_num = SD_CLK_GPIO;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4000;

    // SPI Double-Init Safety Fix
    static bool spi_initialized = false;
    if (!spi_initialized) {
        ret = spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize SPI bus.");
            return false;
        }
        spi_initialized = true;
    }

    // Configure the SD card slot on that SPI bus
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = (gpio_num_t)SD_CS_GPIO;
    slot_config.host_id = SPI3_HOST;

    // Actually mount the file system
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount filesystem.");
        return false;
    }

    ESP_LOGI(TAG, "SD Card mounted at %s", mount_point);
    
    // Print drive details to the serial monitor for debugging
    sdmmc_card_print_info(stdout, card);
    return true;
}

void SDManager::createDefaultDirectories() {
    // The architecture paths we mapped out
    const char* dirs[] = {
        "/sd/games",
        "/sd/games/arduboy",
        "/sd/apps",
        "/sd/saves"
    };

    // Loop through and POSIX stat() check each one
    for (int i = 0; i < 4; i++) {
        struct stat st;
        if (stat(dirs[i], &st) == -1) {
            ESP_LOGI(TAG, "Creating missing directory: %s", dirs[i]);
            // Safety Fix: Check if mkdir fails
            if (mkdir(dirs[i], 0777) != 0) {
                ESP_LOGE(TAG, "Failed to create directory: %s", dirs[i]);
            }
        } else {
            ESP_LOGI(TAG, "Verified directory: %s", dirs[i]);
        }
    }
}

std::vector<std::string> SDManager::getGamesList() {
    std::vector<std::string> games;
    DIR* dir = opendir("/sd/games/arduboy");
    
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open /sd/games/arduboy directory");
        return games; // Return empty vector so the UI doesn't crash
    }

    struct dirent* entry;
    // Read the folder item by item
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) { // If it's a regular file (not a folder)
            std::string filename = entry->d_name;
            
            // Basic verification to only grab .hex files
            if (filename.length() >= 4 && filename.substr(filename.length() - 4) == ".hex") {
                games.push_back(filename);
            }
        }
    }
    closedir(dir);
    return games;
}

std::vector<std::string> SDManager::getFilesInDirectory(std::string path) {
    std::vector<std::string> files;
    DIR* dir = opendir(path.c_str());
    
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", path.c_str());
        return files;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) { // Regular file
            std::string filename = entry->d_name;
            if (filename.length() > 0 && (filename[0] == '.' || filename[0] == '_')) {
                continue;
            }
            if (filename.find('~') != std::string::npos) {
                continue;
            }
            files.push_back(filename);
        }
    }
    closedir(dir);
    return files;
}