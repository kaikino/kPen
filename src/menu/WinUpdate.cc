#ifdef _WIN32

#define NOMINMAX
#include "WinUpdate.h"
#include "version.h"
#include <windows.h>
#include <winhttp.h>
#include <SDL2/SDL.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace {

std::string trimVersion(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end == std::string::npos ? std::string::npos : end - start + 1);
}

// Returns: 1 if a > b, -1 if a < b, 0 if equal
int compareVersions(const std::string& a, const std::string& b) {
    std::string sa = trimVersion(a);
    std::string sb = trimVersion(b);
    for (size_t i = 0, j = 0; i < sa.size() || j < sb.size(); ) {
        int va = 0, vb = 0;
        while (i < sa.size() && sa[i] != '.') { va = va * 10 + (sa[i] - '0'); i++; }
        while (j < sb.size() && sb[j] != '.') { vb = vb * 10 + (sb[j] - '0'); j++; }
        if (va > vb) return 1;
        if (va < vb) return -1;
        if (i < sa.size()) i++;
        if (j < sb.size()) j++;
    }
    return 0;
}

// Minimal JSON parse: find "tag_name":"v1.2.3" and return "1.2.3"
bool parseTagVersion(const char* json, size_t len, char* out, size_t outSize) {
    const char* key = "\"tag_name\":\"";
    const char* p = strstr(json, key);
    if (!p || p - json + strlen(key) >= len) return false;
    p += strlen(key);
    if (*p == 'v') p++;
    const char* end = (const char*)memchr(p, '"', len - (p - json));
    if (!end || (size_t)(end - p) >= outSize) return false;
    size_t n = (size_t)(end - p);
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

// Find Windows installer URL: asset with "win64" in name, return its browser_download_url
bool parseDownloadUrl(const char* json, size_t len, char* out, size_t outSize) {
    const char* p = json;
    const char* end = json + len;
    while (p < end) {
        const char* nameKey = "\"name\":\"";
        p = strstr(p, nameKey);
        if (!p || p + strlen(nameKey) >= end) break;
        const char* nameStart = p + strlen(nameKey);
        const char* nameEnd = (const char*)memchr(nameStart, '"', end - nameStart);
        if (!nameEnd) break;
        bool hasWin64 = (strstr(nameStart, "win64") != nullptr);
        p = nameEnd + 1;
        if (!hasWin64) continue;
        const char* urlKey = "\"browser_download_url\":\"";
        const char* q = strstr(p, urlKey);
        if (!q || q >= end) break;
        q += strlen(urlKey);
        const char* urlEnd = (const char*)memchr(q, '"', end - q);
        if (!urlEnd || (size_t)(urlEnd - q) >= outSize) break;
        size_t n = (size_t)(urlEnd - q);
        memcpy(out, q, n);
        out[n] = '\0';
        return true;
    }
    return false;
}

bool httpGet(const wchar_t* host, const wchar_t* path, std::string* outBody, std::string* outError) {
    HINTERNET session = WinHttpOpen(L"kPen/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { if (outError) *outError = "WinHttpOpen failed"; return false; }
    HINTERNET connect = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) { WinHttpCloseHandle(session); if (outError) *outError = "WinHttpConnect failed"; return false; }
    DWORD flags = WINHTTP_FLAG_SECURE;
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) { WinHttpCloseHandle(connect); WinHttpCloseHandle(session); if (outError) *outError = "WinHttpOpenRequest failed"; return false; }
    BOOL sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!sent) { WinHttpCloseHandle(request); WinHttpCloseHandle(connect); WinHttpCloseHandle(session); if (outError) *outError = "WinHttpSendRequest failed"; return false; }
    if (!WinHttpReceiveResponse(request, nullptr)) { WinHttpCloseHandle(request); WinHttpCloseHandle(connect); WinHttpCloseHandle(session); if (outError) *outError = "WinHttpReceiveResponse failed"; return false; }
    outBody->clear();
    DWORD totalRead = 0;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
        std::string buf(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, &buf[0], available, &read)) break;
        buf.resize(read);
        *outBody += buf;
        totalRead += read;
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return totalRead > 0;
}

bool downloadToFile(const char* url, const wchar_t* filePath, std::string* outError) {
    // Parse URL to get host and path (simple: https://api.github.com/... -> host=api.github.com, path=/...)
    if (strncmp(url, "https://", 8) != 0) { if (outError) *outError = "Only HTTPS supported"; return false; }
    const char* hostStart = url + 8;
    const char* pathStart = strchr(hostStart, '/');
    if (!pathStart) { if (outError) *outError = "Invalid URL"; return false; }
    std::string hostStr(hostStart, pathStart - hostStart);
    std::string pathStr(pathStart);
    std::wstring whost(hostStr.begin(), hostStr.end());
    std::wstring wpath(pathStr.begin(), pathStr.end());
    HINTERNET session = WinHttpOpen(L"kPen/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { if (outError) *outError = "WinHttpOpen failed"; return false; }
    HINTERNET connect = WinHttpConnect(session, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) { WinHttpCloseHandle(session); if (outError) *outError = "WinHttpConnect failed"; return false; }
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", wpath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) { WinHttpCloseHandle(connect); WinHttpCloseHandle(session); if (outError) *outError = "WinHttpOpenRequest failed"; return false; }
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) { WinHttpCloseHandle(request); WinHttpCloseHandle(connect); WinHttpCloseHandle(session); if (outError) *outError = "Send failed"; return false; }
    if (!WinHttpReceiveResponse(request, nullptr)) { WinHttpCloseHandle(request); WinHttpCloseHandle(connect); WinHttpCloseHandle(session); if (outError) *outError = "Receive failed"; return false; }
    HANDLE hFile = CreateFileW(filePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) { WinHttpCloseHandle(request); WinHttpCloseHandle(connect); WinHttpCloseHandle(session); if (outError) *outError = "CreateFile failed"; return false; }
    bool ok = true;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
        std::vector<char> buf(available);
        DWORD read = 0;
        if (!WinHttpReadData(request, buf.data(), available, &read) || read == 0) break;
        DWORD written = 0;
        if (!WriteFile(hFile, buf.data(), read, &written, nullptr) || written != read) { ok = false; break; }
    }
    CloseHandle(hFile);
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    if (!ok && outError) *outError = "Download write failed";
    return ok;
}

void checkThread() {
    WinUpdate::WinUpdateResult* result = new WinUpdate::WinUpdateResult();
    memset(result, 0, sizeof(*result));
    result->has_update = -1;
    std::string body, err;
    if (!httpGet(L"api.github.com", L"/repos/kaikino/kpen/releases/latest", &body, &err)) {
        snprintf(result->error_msg, sizeof(result->error_msg), "%s", err.c_str());
        SDL_Event ev = {};
        ev.type = SDL_USEREVENT;
        ev.user.code = WinUpdate::WIN_UPDATE_RESULT;
        ev.user.data1 = result;
        ev.user.data2 = nullptr;
        SDL_PushEvent(&ev);
        return;
    }
    char tagVer[32], url[512];
    if (!parseTagVersion(body.c_str(), body.size(), tagVer, sizeof(tagVer))) {
        snprintf(result->error_msg, sizeof(result->error_msg), "No version in response");
        SDL_Event ev = {};
        ev.type = SDL_USEREVENT;
        ev.user.code = WinUpdate::WIN_UPDATE_RESULT;
        ev.user.data1 = result;
        ev.user.data2 = nullptr;
        SDL_PushEvent(&ev);
        return;
    }
    std::string currentVersion = trimVersion(KPEN_VERSION_STRING);
    bool unknownVersion = (currentVersion.empty() || currentVersion == "0.0.0");
    if (unknownVersion) {
        result->has_update = 0;
        strncpy(result->version, tagVer, sizeof(result->version) - 1);
        result->version[sizeof(result->version) - 1] = '\0';
        SDL_Event ev = {};
        ev.type = SDL_USEREVENT;
        ev.user.code = WinUpdate::WIN_UPDATE_RESULT;
        ev.user.data1 = result;
        ev.user.data2 = nullptr;
        SDL_PushEvent(&ev);
        return;
    }
    if (compareVersions(tagVer, currentVersion) <= 0) {
        result->has_update = 0;
        strncpy(result->version, currentVersion.c_str(), sizeof(result->version) - 1);
        SDL_Event ev = {};
        ev.type = SDL_USEREVENT;
        ev.user.code = WinUpdate::WIN_UPDATE_RESULT;
        ev.user.data1 = result;
        ev.user.data2 = nullptr;
        SDL_PushEvent(&ev);
        return;
    }
    if (!parseDownloadUrl(body.c_str(), body.size(), url, sizeof(url))) {
        snprintf(result->error_msg, sizeof(result->error_msg), "No Windows installer in release");
        SDL_Event ev = {};
        ev.type = SDL_USEREVENT;
        ev.user.code = WinUpdate::WIN_UPDATE_RESULT;
        ev.user.data1 = result;
        ev.user.data2 = nullptr;
        SDL_PushEvent(&ev);
        return;
    }
    result->has_update = 1;
    strncpy(result->version, tagVer, sizeof(result->version) - 1);
    strncpy(result->url, url, sizeof(result->url) - 1);
    SDL_Event ev = {};
    ev.type = SDL_USEREVENT;
    ev.user.code = WinUpdate::WIN_UPDATE_RESULT;
    ev.user.data1 = result;
    ev.user.data2 = nullptr;
    SDL_PushEvent(&ev);
}

void downloadThread(const std::string& url) {
    wchar_t tempPath[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tempPath) == 0) return;
    std::wstring wpath = std::wstring(tempPath) + L"kPen-installer.exe";
    std::string err;
    if (!downloadToFile(url.c_str(), wpath.c_str(), &err)) return;
    size_t len = wpath.size() + 1;
    char* pathA = (char*)malloc(len * 2);
    if (!pathA) return;
    size_t converted = 0;
    wcstombs_s(&converted, pathA, len * 2, wpath.c_str(), _TRUNCATE);
    SDL_Event ev = {};
    ev.type = SDL_USEREVENT;
    ev.user.code = WinUpdate::WIN_INSTALL_LAUNCH;
    ev.user.data1 = pathA;
    ev.user.data2 = nullptr;
    SDL_PushEvent(&ev);
}

} // namespace

namespace WinUpdate {

void startCheckAsync() {
    std::thread t(checkThread);
    t.detach();
}

void startDownloadAndInstall(const char* url) {
    if (!url || !url[0]) return;
    std::thread t(downloadThread, std::string(url));
    t.detach();
}

} // namespace WinUpdate

#endif
