#pragma once
#include <string>
#include <vector>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

class SDManager {
public:
    static SDManager& getInstance() {
        static SDManager instance;
        return instance;
    }

    SDManager(SDManager const&) = delete;
    void operator=(SDManager const&) = delete;

    // Bootup routine
    bool initialize();
    
    // File system access
    std::vector<std::string> getGamesList();
    std::vector<std::string> getFilesInDirectory(std::string path);

private:
    SDManager() = default;
    ~SDManager() = default;

    bool mountFileSystem();
    void createDefaultDirectories();

    sdmmc_card_t* card;
    const char* mount_point = "/sd";
};