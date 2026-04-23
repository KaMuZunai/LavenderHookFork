#pragma once

#include "../aobhooks/EnemiesAliveHook.h"
#include "../aobhooks/WaveDataHook.h"
#include "../aobhooks/TimeHook.h"
#include "../aobhooks/EnemiesMaxFallbackHook.h"
#include "../aobhooks/ManaHook.h"

namespace LavenderHook::Memory {

    inline bool Initialize()
    {
        const bool a = EnemiesAliveHook::Initialize();
        const bool b = WaveDataHook::Initialize();
        const bool c = TimeHook::Initialize();
        const bool d = EnemiesMaxFallbackHook::Initialize();
        const bool e = ManaHook::Initialize();
        return a || b || c || d || e;
    }

    inline void Shutdown()
    {
        EnemiesAliveHook::Shutdown();
        WaveDataHook::Shutdown();
        TimeHook::Shutdown();
        EnemiesMaxFallbackHook::Shutdown();
        ManaHook::Shutdown();
    }

    // Call once per frame to update polled globals (e.g. Globals::mana_max).
    inline void Tick()
    {
        ManaHook::Tick();
    }

} // namespace LavenderHook::Memory

