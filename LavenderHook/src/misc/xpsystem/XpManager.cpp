#include "XpManager.h"
#include "XpTables.h"
#include "../../ui/components/Console.h"

#include <windows.h>
#include <cmath>
#include <mutex>
#include <string>
#include <sstream>
#include <iomanip>

namespace LavenderHook {
    namespace XpManager {

        static std::mutex s_mutex;
        static int   s_level = 1;
        static float s_currentXp = 0.f;

        // Persistence helpers
        static std::string GetIniPath()
        {
            char appdata[MAX_PATH]; size_t outlen = 0;
            getenv_s(&outlen, appdata, sizeof(appdata), "APPDATA");
            std::string dir(appdata);
            dir += "\\LavenderHook";
            CreateDirectoryA(dir.c_str(), nullptr);
            return dir + "\\Paragon.ini";
        }

        static void LoadFromDisk()
        {
            const std::string path = GetIniPath();
            const char* p = path.c_str();

            s_level = GetPrivateProfileIntA("XP", "Level", 1, p);
            if (s_level < 1) s_level = 1;

            char buf[64] = {};
            GetPrivateProfileStringA("XP", "XP", "0", buf, sizeof(buf), p);
            try { s_currentXp = std::stof(buf); }
            catch (...) { s_currentXp = 0.f; }
            if (s_currentXp < 0.f) s_currentXp = 0.f;
        }

        static void SaveToDisk()
        {
            const std::string path = GetIniPath();
            const char* p = path.c_str();
            WritePrivateProfileStringA("XP", "Level", std::to_string(s_level).c_str(), p);
            WritePrivateProfileStringA("XP", "XP", std::to_string(static_cast<int>(s_currentXp)).c_str(), p);
        }

        void Init()
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            LoadFromDisk();
        }

        int GetLevel()
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            return s_level;
        }

        float GetCurrentXp()
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            return s_currentXp;
        }

        float GetThreshold()
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            return XpTables::GetXpThreshold(s_level);
        }

        void AwardXp(float amount)
        {
            if (amount <= 0.f) return;

            std::lock_guard<std::mutex> lock(s_mutex);
            s_currentXp = std::round(s_currentXp + amount);

            while (true) {
                const float threshold = XpTables::GetXpThreshold(s_level);
                if (s_currentXp < threshold) break;
                s_currentXp -= threshold;
                ++s_level;
                try {
                    LavenderConsole::GetInstance().Log(
                        "[XP] Level up! -> Level " + std::to_string(s_level));
                }
                catch (...) {}
            }

            try {
                const float threshold = XpTables::GetXpThreshold(s_level);
                LavenderConsole::GetInstance().Log(
                    "[XP] +" + std::to_string(static_cast<int>(amount))
                    + " XP  |  Level " + std::to_string(s_level)
                    + ": " + std::to_string(static_cast<int>(s_currentXp))
                    + " / " + std::to_string(static_cast<int>(threshold)) + " XP");
            }
            catch (...) {}

            SaveToDisk();
        }

    } // namespace XpManager
} // namespace LavenderHook
