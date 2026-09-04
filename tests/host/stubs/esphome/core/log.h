#pragma once

#define ESPHOME_LOG_LEVEL 0
#define ESPHOME_LOG_LEVEL_VERBOSE 5
#define ESP_LOGCONFIG(...) ((void) 0)
#define ESP_LOGD(...) ((void) 0)
#define ESP_LOGE(...) ((void) 0)
#define ESP_LOGI(...) ((void) 0)
#define ESP_LOGV(...) ((void) 0)
#define ESP_LOGW(...) ((void) 0)
#define LOG_BINARY_SENSOR(...) ((void) 0)
#define LOG_SELECT(...) ((void) 0)
#define ONOFF(value) ((value) ? "ON" : "OFF")
#define YESNO(value) ((value) ? "YES" : "NO")
