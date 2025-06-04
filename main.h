#ifndef MAIN_H
#define MAIN_H

// ログレベルの定義
enum LogLevel {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN  = 1,
    LOG_LEVEL_INFO  = 2,
    LOG_LEVEL_DEBUG = 3
};

#ifdef __cplusplus
extern "C" {
#endif

extern int debug_level;
void log_printf(int level, const char* format, ...);

#ifdef __cplusplus
}
#endif

// C++リンケージで宣言
#ifdef __cplusplus
void safe_printf(const char* format, ...);
#endif

#endif // MAIN_H 