#pragma once

#include "../aobhooks/EnemiesAliveHook.h"
#include "../aobhooks/WaveDataHook.h"
#include "../aobhooks/TimeHook.h"
#include "../aobhooks/EnemiesMaxFallbackHook.h"

namespace LavenderHook::Memory {

    inline bool Initialize()
    {
        const bool a = EnemiesAliveHook::Initialize();
        const bool b = WaveDataHook::Initialize();
        const bool c = TimeHook::Initialize();
        const bool d = EnemiesMaxFallbackHook::Initialize();
        return a || b || c || d;
    }

    inline void Shutdown()
    {
        EnemiesAliveHook::Shutdown();
        WaveDataHook::Shutdown();
        TimeHook::Shutdown();
        EnemiesMaxFallbackHook::Shutdown();
    }

} // namespace LavenderHook::Memory

