#include "MenuState.h"
#include "BatteryManager.h"
#include "BoardConfig.h"
#include "DisplayManager.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "InputManager.h"
#include "SDManager.h"
#include "StateManager.h"
#include "types.h"
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <stdio.h>

static const char *TAG = "MenuState";

MenuState &MenuState::getInstance() {
  static MenuState instance;
  return instance;
}

void MenuState::loadRoot() {
  m_inRoot = true;
  m_currentPath = "";
  m_gamesList.clear();
  m_gamesList.push_back("apps");
  m_gamesList.push_back("games");
  m_gamesList.push_back("Sleep");
  m_cursorIndex = m_rootCursorIndex;
  m_topVisibleIndex = m_rootTopVisibleIndex;
}

void MenuState::loadDirectory(const std::string &path) {
  if (m_inRoot) {
    m_rootCursorIndex = m_cursorIndex;
    m_rootTopVisibleIndex = m_topVisibleIndex;
  }
  m_inRoot = false;
  m_currentPath = path;
  m_gamesList = SDManager::getInstance().getFilesInDirectory(path);
  m_cursorIndex = 0;
  m_topVisibleIndex = 0;
}

void MenuState::onEnter() {
  ESP_LOGI(TAG, "Entering Menu State...");
  sysContext.current_state.store(SystemState::MainMenu);

  m_lastBatteryCheckTime = esp_timer_get_time();
  m_cachedBatteryPct = BatteryManager::getInstance().getBatteryPercentage();
  m_inDetailView = false;

  loadRoot();
}

void MenuState::onUpdate() {
  // 10-second non-blocking battery check
  if (esp_timer_get_time() - m_lastBatteryCheckTime > 10000000) {
    m_lastBatteryCheckTime = esp_timer_get_time();
    m_cachedBatteryPct = BatteryManager::getInstance().getBatteryPercentage();
  }

  if (m_inDetailView) {
    // Only B button dismisses details screen
    if (InputManager::getInstance().justPressed(5)) { // BTN_B
      m_inDetailView = false;
    }
    return; // Block other inputs while in details view
  }

  // 0 is BTN_UP
  if (InputManager::getInstance().justPressed(0)) {
    if (m_cursorIndex > 0) {
      m_cursorIndex--;
      if (m_cursorIndex < m_topVisibleIndex) {
        m_topVisibleIndex = m_cursorIndex;
      }
    }
  }

  // 1 is BTN_DOWN
  if (InputManager::getInstance().justPressed(1)) {
    if (m_cursorIndex < (int)m_gamesList.size() - 1) {
      m_cursorIndex++;
      if (m_cursorIndex >= m_topVisibleIndex + MAX_VISIBLE_ITEMS) {
        m_topVisibleIndex = m_cursorIndex - MAX_VISIBLE_ITEMS + 1;
      }
    }
  }

  // 4 is BTN_A
  if (InputManager::getInstance().justPressed(4)) {
    if (m_inRoot && m_gamesList.size() > 0) {
      std::string selection = m_gamesList[m_cursorIndex];
      if (selection == "apps") {
        loadDirectory("/sd/apps");
      } else if (selection == "games") {
        loadDirectory("/sd/games/arduboy");
      } else if (selection == "Sleep") {
        ESP_LOGI(TAG, "Initiating sleep sequence...");
        DisplayManager::getInstance().sleepDisplay();

        gpio_wakeup_enable((gpio_num_t)BTN_START_GPIO, GPIO_INTR_LOW_LEVEL);
        esp_sleep_enable_gpio_wakeup();

        esp_light_sleep_start();

        DisplayManager::getInstance().wakeDisplay();
      }
    } else if (!m_inRoot && m_gamesList.size() > 0) {
      std::string selection = m_gamesList[m_cursorIndex];
      std::string absolutePath = m_currentPath + "/" + selection;
      
      ESP_LOGI(TAG, "Initiating OTA Flash for %s", absolutePath.c_str());
      
      // 1. Draw "Flashing Game..." UI
      DisplayManager::getInstance().clearBuffer();
      DisplayManager::getInstance().drawText(10, 20, "Flashing Game...");
      DisplayManager::getInstance().drawText(10, 30, selection.c_str());
      DisplayManager::getInstance().drawText(10, 50, "Please Wait!");
      DisplayManager::getInstance().renderPipelinePush();
      
      // 2. Open .bin file
      FILE* f = fopen(absolutePath.c_str(), "rb");
      if (!f) {
          ESP_LOGE(TAG, "Failed to open %s", absolutePath.c_str());
          DisplayManager::getInstance().clearBuffer();
          DisplayManager::getInstance().drawText(10, 30, "ERROR: FILE OPEN");
          DisplayManager::getInstance().renderPipelinePush();
          vTaskDelay(pdMS_TO_TICKS(2000));
          return;
      }
      
      // 3. Locate ota_1 partition
      const esp_partition_t* update_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
      if (!update_partition) {
          ESP_LOGE(TAG, "Failed to find ota_1 partition");
          fclose(f);
          DisplayManager::getInstance().clearBuffer();
          DisplayManager::getInstance().drawText(10, 30, "ERROR: NO OTA_1");
          DisplayManager::getInstance().renderPipelinePush();
          vTaskDelay(pdMS_TO_TICKS(2000));
          return;
      }
      
      // 4. Begin OTA
      esp_ota_handle_t update_handle = 0;
      esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
      if (err != ESP_OK) {
          ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
          fclose(f);
          DisplayManager::getInstance().clearBuffer();
          DisplayManager::getInstance().drawText(10, 30, "ERROR: OTA BEGIN");
          DisplayManager::getInstance().renderPipelinePush();
          vTaskDelay(pdMS_TO_TICKS(2000));
          return;
      }
      
      // 5. Write in chunks (yielding to WDT!)
      uint8_t buffer[1024];
      size_t read_bytes;
      bool success = true;
      while ((read_bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
          err = esp_ota_write(update_handle, buffer, read_bytes);
          if (err != ESP_OK) {
              ESP_LOGE(TAG, "esp_ota_write failed (%s)", esp_err_to_name(err));
              success = false;
              break;
          }
          // Yield to feed the Core 1 Watchdog Timer
          vTaskDelay(1);
      }
      fclose(f);
      
      // 6. End OTA
      if (!success) {
          esp_ota_abort(update_handle);
          DisplayManager::getInstance().clearBuffer();
          DisplayManager::getInstance().drawText(10, 30, "ERROR: OTA WRITE");
          DisplayManager::getInstance().renderPipelinePush();
          vTaskDelay(pdMS_TO_TICKS(2000));
          return;
      }
      
      err = esp_ota_end(update_handle);
      if (err != ESP_OK) {
          ESP_LOGE(TAG, "esp_ota_end failed (%s)", esp_err_to_name(err));
          DisplayManager::getInstance().clearBuffer();
          DisplayManager::getInstance().drawText(10, 30, "ERROR: OTA END");
          DisplayManager::getInstance().renderPipelinePush();
          vTaskDelay(pdMS_TO_TICKS(2000));
          return;
      }
      
      err = esp_ota_set_boot_partition(update_partition);
      if (err != ESP_OK) {
          ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)", esp_err_to_name(err));
          DisplayManager::getInstance().clearBuffer();
          DisplayManager::getInstance().drawText(10, 30, "ERROR: BOOT SET");
          DisplayManager::getInstance().renderPipelinePush();
          vTaskDelay(pdMS_TO_TICKS(2000));
          return;
      }
      
      ESP_LOGI(TAG, "OTA Success! Rebooting into game...");
      DisplayManager::getInstance().clearBuffer();
      DisplayManager::getInstance().drawText(10, 30, "FLASH SUCCESS!");
      DisplayManager::getInstance().drawText(10, 40, "REBOOTING...");
      DisplayManager::getInstance().renderPipelinePush();
      vTaskDelay(pdMS_TO_TICKS(1000));
      
      esp_restart();
    }
  }

  // 5 is BTN_B
  if (InputManager::getInstance().justPressed(5)) {
    if (!m_inRoot) {
      loadRoot();
    }
  }

  // 6 is BTN_START, 7 is BTN_SELECT
  if (InputManager::getInstance().justPressed(6) ||
      InputManager::getInstance().justPressed(7)) {
    if (!m_inRoot && m_gamesList.size() > 0) {
      m_inDetailView = true;
    }
  }
}

void MenuState::onDraw() {
  DisplayManager::getInstance().clearBuffer();
  if (m_inDetailView) {
    DisplayManager::getInstance().drawText(0, 0, "FILE DETAILS:");
    DisplayManager::getInstance().drawLine(0, 7, 127, 7, 1);

    std::string fullName = m_gamesList[m_cursorIndex];
    int y = 10;
    int x = 0;
    for (char c : fullName) {
      DisplayManager::getInstance().drawChar(x, y, c);
      x += 6;
      if (x > 122) { // Screen is 128px wide. 128 - 6 = 122
        x = 0;
        y += 10;
      }
    }
    return;
  }

  // 1. Draw static header (0-7px)
  DisplayManager::getInstance().drawText(0, 0, "MENU");

  char bat_str[16];
  snprintf(bat_str, sizeof(bat_str), "%d%%", m_cachedBatteryPct);

  // Calculate right alignment: approx 6 pixels per character
  int bat_x = 128 - (snprintf(NULL, 0, "%d%%", m_cachedBatteryPct) * 6);
  if (bat_x < 0)
    bat_x = 0;
  DisplayManager::getInstance().drawText(bat_x, 0, bat_str);

  // Draw header divider
  DisplayManager::getInstance().drawLine(0, 7, 127, 7, 1);

  // 2. Draw slots
  int endIndex = m_topVisibleIndex + MAX_VISIBLE_ITEMS;
  if (endIndex > (int)m_gamesList.size()) {
    endIndex = (int)m_gamesList.size();
  }

  bool is_blink_on = (esp_timer_get_time() / 400000) % 2 == 0;

  for (int i = 0; i < MAX_VISIBLE_ITEMS; i++) {
    int list_index = m_topVisibleIndex + i;
    int y_pos = 8 + (i * 8); // Start at row 8

    // Draw bottom divider for this slot
    DisplayManager::getInstance().drawLine(0, y_pos + 7, 127, y_pos + 7, 1);

    if (list_index < (int)m_gamesList.size()) {
      bool selected = (list_index == m_cursorIndex);

      std::string displayName = m_gamesList[list_index];
      if (!m_inRoot) {
        // Strip .hex or .HEX extension
        size_t dotPos = displayName.rfind(".hex");
        if (dotPos == std::string::npos)
          dotPos = displayName.rfind(".HEX");
        if (dotPos != std::string::npos) {
          displayName = displayName.substr(0, dotPos);
        }
      }

      // Truncate to ~18 characters to leave room for the cursor
      if (displayName.length() > 18) {
        displayName = displayName.substr(0, 15) + "...";
      }

      if (selected && is_blink_on) {
        // Inverted background
        DisplayManager::getInstance().fillRect(0, y_pos, 128, 7, 1);
        // Inverted text
        DisplayManager::getInstance().drawText(8, y_pos, displayName.c_str(), 0,
                                               1);
      } else {
        // Normal text
        if (selected) {
          DisplayManager::getInstance().drawText(0, y_pos, ">");
        }
        DisplayManager::getInstance().drawText(8, y_pos, displayName.c_str());
      }
    }
  }
}

void MenuState::onExit() { ESP_LOGI(TAG, "Exiting Menu State..."); }
