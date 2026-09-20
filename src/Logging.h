#ifndef ECOSMART_LOGGING_H
#define ECOSMART_LOGGING_H

// ============================================================
// EcoSmart logging facade
//
// IMPORTANT — arduino-esp32 reality check:
//
// Under the Arduino framework, esp32-hal-log.h redefines the
// ESP_LOGx macros to route through ARDUHAL. The tag is injected
// into the message body and file/line/function are prepended,
// so the actual output is:
//
//     [<ms>][I][<file>:<line>] <func>(): [<TAG>] <message>
//
// Example:
//
//     [730][I][CloudRegistryStore.cpp:44] begin(): [CLOUD-REG-STORE] Loaded from NVS:
//
// Consequences:
//   - Callers MUST NOT put the tag inside the format string;
//     ARDUHAL adds it for you.
//   - Filtering is COMPILE-TIME via CORE_DEBUG_LEVEL, set in
//     platformio.ini:
//         build_flags = -DCORE_DEBUG_LEVEL=3
//     (0=None 1=Error 2=Warn 3=Info 4=Debug 5=Verbose)
//   - esp_log_level_set() has NO effect on ARDUHAL macros.
//     ECOSMART_LOG_SET_LEVEL() is therefore a no-op here and is
//     kept only so call sites stay portable.
//
// Define ECOSMART_LOG_USE_IDF to bypass ARDUHAL and use the
// native IDF logger, which does support per-tag runtime levels.
// ============================================================

#if defined(ECOSMART_LOG_USE_IDF)

  #include <esp_log.h>

  #define ECOSMART_LOGE(tag, fmt, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_ERROR,   tag, fmt, ##__VA_ARGS__)
  #define ECOSMART_LOGW(tag, fmt, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_WARN,    tag, fmt, ##__VA_ARGS__)
  #define ECOSMART_LOGI(tag, fmt, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_INFO,    tag, fmt, ##__VA_ARGS__)
  #define ECOSMART_LOGD(tag, fmt, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_DEBUG,   tag, fmt, ##__VA_ARGS__)
  #define ECOSMART_LOGV(tag, fmt, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_VERBOSE, tag, fmt, ##__VA_ARGS__)

  #define ECOSMART_LOG_SET_LEVEL(tag, level) esp_log_level_set(tag, level)

  #define ECOSMART_LOG_LEVEL_NONE    ESP_LOG_NONE
  #define ECOSMART_LOG_LEVEL_ERROR   ESP_LOG_ERROR
  #define ECOSMART_LOG_LEVEL_WARN    ESP_LOG_WARN
  #define ECOSMART_LOG_LEVEL_INFO    ESP_LOG_INFO
  #define ECOSMART_LOG_LEVEL_DEBUG   ESP_LOG_DEBUG
  #define ECOSMART_LOG_LEVEL_VERBOSE ESP_LOG_VERBOSE

#else  // ARDUHAL (default under PlatformIO + arduino framework)

  #include <esp32-hal-log.h>

  #define ECOSMART_LOGE(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
  #define ECOSMART_LOGW(tag, fmt, ...) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
  #define ECOSMART_LOGI(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
  #define ECOSMART_LOGD(tag, fmt, ...) ESP_LOGD(tag, fmt, ##__VA_ARGS__)
  #define ECOSMART_LOGV(tag, fmt, ...) ESP_LOGV(tag, fmt, ##__VA_ARGS__)

  #define ECOSMART_LOG_SET_LEVEL(tag, level) ((void)(tag), (void)(level))

  #define ECOSMART_LOG_LEVEL_NONE    0
  #define ECOSMART_LOG_LEVEL_ERROR   1
  #define ECOSMART_LOG_LEVEL_WARN    2
  #define ECOSMART_LOG_LEVEL_INFO    3
  #define ECOSMART_LOG_LEVEL_DEBUG   4
  #define ECOSMART_LOG_LEVEL_VERBOSE 5

#endif

#endif 