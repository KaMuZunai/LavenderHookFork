#pragma once

#include <string>
#include <atomic>

namespace LavenderHook {
    namespace LogMonitor {

        struct SessionInfo {
            std::string gameMode;
            std::string gameDifficulty;
            std::string mapName;
            std::string bonusWave;      // can be empty
            bool hardcore = false;
            bool riftMode = false;
            bool valid    = false;
        };

        void Start();
        void Stop();

        bool        LatestLineHasAbort();
        SessionInfo GetCurrentSession();
    }
}
