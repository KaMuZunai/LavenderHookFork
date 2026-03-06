#pragma once

namespace LavenderHook {
    namespace XpManager {

        void  Init();

        void  AwardXp(float amount);
        int   GetLevel();
        float GetCurrentXp();
        float GetThreshold();

    } // namespace XpManager
} // namespace LavenderHook
