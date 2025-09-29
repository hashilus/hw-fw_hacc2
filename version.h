#ifndef VERSION_H
#define VERSION_H

// バージョン情報
#define VERSION_MAJOR 2
#define VERSION_MINOR 1
#define VERSION_PATCH 2

// バージョン文字列を自動生成
#define VERSION_STRINGIFY(x) #x
#define VERSION_STRINGIFY2(x) VERSION_STRINGIFY(x)
#define VERSION_STRING VERSION_STRINGIFY2(VERSION_MAJOR) "." VERSION_STRINGIFY2(VERSION_MINOR) "." VERSION_STRINGIFY2(VERSION_PATCH)

// デバイス名
#define DEVICE_NAME "HACC2"

// バージョン情報を文字列として取得する関数
inline const char* getVersionString() {
    return VERSION_STRING;
}

// バージョン情報を構造体として取得する関数
struct VersionInfo {
    int major;
    int minor;
    int patch;
    const char* version;
    const char* device;
};

inline VersionInfo getVersionInfo() {
    return {
        VERSION_MAJOR,
        VERSION_MINOR,
        VERSION_PATCH,
        VERSION_STRING,
        DEVICE_NAME
    };
}

#endif // VERSION_H 