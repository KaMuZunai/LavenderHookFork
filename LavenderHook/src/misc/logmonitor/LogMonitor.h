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
            int         startWave = 0;  // starting wave from TryHostGame (0 if not specified)
            bool hardcore = false;
            bool riftMode = false;
            bool valid    = false;
        };

        void Start();
        void Stop();

        bool        LatestLineHasAbort();
        SessionInfo GetCurrentSession();
        int         GetCurrentWave();
        bool        IsInTavern();
        bool        IsBossWave();
        bool        IsCombatPhase();
        bool        IsCombatAborted();
        bool        IsPreSummaryPhase();
        bool        IsFinalBuildPhaseEnded();
        int         GetMaxWave();
    }
}
