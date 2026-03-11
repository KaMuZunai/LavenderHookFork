#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define _WIN32_WINNT 0x0A00
#include <sdkddkver.h>
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <sstream>

#include "Updater.h"
#include "../misc/Globals.h"

namespace LavenderHook::Updater
{
    static std::vector<int> ParseVersion(const std::string& ver)
    {
        std::vector<int> parts;
        std::istringstream ss(ver);
        std::string token;
        while (std::getline(ss, token, '.'))
        {
            try { parts.push_back(std::stoi(token)); }
            catch (...) { parts.push_back(0); }
        }
        return parts;
    }

    static bool IsVersionLower(const std::vector<int>& a, const std::vector<int>& b)
    {
        size_t len = (a.size() > b.size()) ? a.size() : b.size();
        for (size_t i = 0; i < len; ++i)
        {
            int av = (i < a.size()) ? a[i] : 0;
            int bv = (i < b.size()) ? b[i] : 0;
            if (av < bv) return true;
            if (av > bv) return false;
        }
        return false;
    }

    static std::string GetDllFileVersion(const std::wstring& path)
    {
        DWORD dummy = 0;
        DWORD size = GetFileVersionInfoSizeW(path.c_str(), &dummy);
        if (size == 0) return "";

        std::vector<BYTE> data(size);
        if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()))
            return "";

        VS_FIXEDFILEINFO* ffi = nullptr;
        UINT ffiLen = 0;
        if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<LPVOID*>(&ffi), &ffiLen))
            return "";

        int major    = HIWORD(ffi->dwFileVersionMS);
        int minor    = LOWORD(ffi->dwFileVersionMS);
        int build    = HIWORD(ffi->dwFileVersionLS);
        int revision = LOWORD(ffi->dwFileVersionLS);

        return std::to_string(major) + "." + std::to_string(minor) + "." +
               std::to_string(build) + "." + std::to_string(revision);
    }

    static std::string FetchHttps(const std::wstring& host, const std::wstring& urlPath)
    {
        std::string result;

        HINTERNET hSession = WinHttpOpen(
            L"LavenderHook-Updater/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);
        if (!hSession) return result;

        HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return result; }

        HINTERNET hRequest = WinHttpOpenRequest(
            hConnect,
            L"GET",
            urlPath.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE);
        if (!hRequest)
        {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return result;
        }

        DWORD dwRedirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &dwRedirect, sizeof(dwRedirect));

        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hRequest, nullptr))
        {
            DWORD bytesAvail = 0;
            while (WinHttpQueryDataAvailable(hRequest, &bytesAvail) && bytesAvail > 0)
            {
                std::string chunk(bytesAvail, '\0');
                DWORD bytesRead = 0;
                if (WinHttpReadData(hRequest, &chunk[0], bytesAvail, &bytesRead))
                    result.append(chunk.data(), bytesRead);
            }
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    static bool DownloadFileHttps(const std::wstring& host, const std::wstring& urlPath,
                                  const std::wstring& destPath)
    {
        bool success = false;

        HINTERNET hSession = WinHttpOpen(
            L"LavenderHook-Updater/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);
        if (!hSession) return false;

        HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

        HINTERNET hRequest = WinHttpOpenRequest(
            hConnect,
            L"GET",
            urlPath.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE);
        if (!hRequest)
        {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }

        DWORD dwRedirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &dwRedirect, sizeof(dwRedirect));

        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hRequest, nullptr))
        {
            std::wstring tempPath = destPath + L".tmp";
            HANDLE hFile = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0,
                                       nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile != INVALID_HANDLE_VALUE)
            {
                DWORD bytesAvail = 0;
                success = true;
                while (WinHttpQueryDataAvailable(hRequest, &bytesAvail) && bytesAvail > 0)
                {
                    std::vector<char> buf(bytesAvail);
                    DWORD bytesRead = 0;
                    if (!WinHttpReadData(hRequest, buf.data(), bytesAvail, &bytesRead))
                    {
                        success = false;
                        break;
                    }
                    DWORD written = 0;
                    WriteFile(hFile, buf.data(), bytesRead, &written, nullptr);
                }
                CloseHandle(hFile);

                if (success)
                {
                    success = MoveFileExW(tempPath.c_str(), destPath.c_str(),
                                         MOVEFILE_REPLACE_EXISTING) != FALSE;
                }
                else
                {
                    DeleteFileW(tempPath.c_str());
                }
            }
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return success;
    }

    void RunUpdater()
    {
        wchar_t selfPath[MAX_PATH] = {};
        GetModuleFileNameW(Globals::dll_module, selfPath, MAX_PATH);

        std::wstring dllDir(selfPath);
        size_t lastSlash = dllDir.find_last_of(L"\\/");
        std::wstring rawUpdaterPath;
        if (lastSlash != std::wstring::npos)
            rawUpdaterPath = dllDir.substr(0, lastSlash) + L"\\..\\LavenderUpdater.dll";
        else
            rawUpdaterPath = L"..\\LavenderUpdater.dll";

        wchar_t resolvedPath[MAX_PATH] = {};
        GetFullPathNameW(rawUpdaterPath.c_str(), MAX_PATH, resolvedPath, nullptr);
        std::wstring updaterPath(resolvedPath);

        std::string localVerStr = GetDllFileVersion(updaterPath);
        std::vector<int> localVer = ParseVersion(localVerStr);

        std::string remoteVerStr = FetchHttps(
            L"raw.githubusercontent.com",
            L"/CoolGamer2000-dda/LavenderHookFork/refs/heads/main/UpdaterVersion");

        while (!remoteVerStr.empty() &&
               (remoteVerStr.back() == '\r' || remoteVerStr.back() == '\n' ||
                remoteVerStr.back() == ' '))
        {
            remoteVerStr.pop_back();
        }

        if (remoteVerStr.empty())
            return;

        std::vector<int> remoteVer = ParseVersion(remoteVerStr);

        if (!IsVersionLower(localVer, remoteVer))
            return;

        Sleep(3000);

        // Force-unload LavenderUpdater.dll
        {
            HMODULE hUpdater = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               updaterPath.c_str(), &hUpdater);
            while (hUpdater && FreeLibrary(hUpdater))
            {
                hUpdater = nullptr;
                GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   updaterPath.c_str(), &hUpdater);
            }
        }

        DownloadFileHttps(
            L"github.com",
            L"/CoolGamer2000-dda/LavenderHook/releases/latest/download/LavenderUpdater.dll",
            updaterPath);
    }
}
