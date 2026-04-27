#pragma once

#include <windows.h>
#include <cstdint>
#include <cstring>
#include "../aobutils/ScannerUtils.h"
#include "../../misc/Globals.h"

namespace LavenderHook::Memory::EnemiesMaxFallbackHook {

    // Enemies Max Fallback incase WaveDataHook misses because its not a boss map.

    // MOV [RBX+0x488], ESI
    inline constexpr uint8_t k_Pattern[] = {
        0x89, 0xB3, 0x88, 0x04, 0x00, 0x00, // MOV [RBX+0x488], ESI
        0xE8, 0x71, 0x6E, 0x1A, 0xFF,       // CALL ... (wildcarded offset)
        0x48, 0x8B, 0xCB,                    // MOV RCX, RBX
        0xE8, 0x09, 0x61, 0xFE, 0xFF,       // CALL ...
        0x48, 0x8B, 0x87, 0xB8, 0x04, 0x00, 0x00, // MOV RAX, [RBX+0x4B8]
        0x89, 0xA8, 0x8C, 0x04, 0x00, 0x00, // MOV [RAX+0x48C], EBP
        0x48, 0x8B, 0x9C, 0x24, 0x80, 0x00, 0x00, 0x00, // MOV RBX, [RSP+0x80]
        0x48, 0x83, 0xC4, 0x40,             // ADD RSP, 0x40
        0x41, 0x5F,                         // POP R15
        0x41, 0x5E                          // POP R14
    };

    // 1 = match, 0 = wildcard so it stops breaking on updates >:(
    inline constexpr uint8_t k_Mask[] = {
        1, 1, 1, 1, 1, 1,
        1, 0, 0, 0, 0,
        1, 1, 1,
        1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1,
        1, 1,
        1, 1
    };

    inline void*     g_stub   = nullptr;
    inline uintptr_t g_hookAt = 0;

    inline bool InstallHook(uint8_t* hookAt)
    {
        void* stub = Utils::AllocNear(reinterpret_cast<uintptr_t>(hookAt), 64);
        if (!stub) return false;

        const uintptr_t maxAddr = reinterpret_cast<uintptr_t>(&Globals::enemies_max);
        const uintptr_t retAddr = reinterpret_cast<uintptr_t>(hookAt) + 6;

        uint8_t* p = static_cast<uint8_t*>(stub);

        // [+00] Original MOV [RBX+0x488], ESI (6 bytes)
        *p++ = 0x89; *p++ = 0xB3;
        *p++ = 0x88; *p++ = 0x04; *p++ = 0x00; *p++ = 0x00;

        // [+06] Save RAX
        *p++ = 0x50;                                               // PUSH RAX

        // [+07] Load address of Globals::enemies_max into RAX
        *p++ = 0x48; *p++ = 0xB8;                                 // MOV RAX, imm64
        std::memcpy(p, &maxAddr, 8); p += 8;

        // [+11] MOV [RAX], ESI write max count to Globals::enemies_max
        *p++ = 0x89; *p++ = 0x30;

        // [+13] Restore RAX
        *p++ = 0x58;                                               // POP RAX

        // [+14] JMP [RIP+0] -> retAddr (hookAt+6)
        *p++ = 0xFF; *p++ = 0x25;
        *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
        std::memcpy(p, &retAddr, 8); p += 8;

        g_stub   = stub;
        g_hookAt = reinterpret_cast<uintptr_t>(hookAt);

        // Patch hookAt[0-4] with E9 <rel32>; hookAt[5] becomes NOP.
        DWORD oldProt = 0;
        VirtualProtect(hookAt, 6, PAGE_EXECUTE_READWRITE, &oldProt);
        const int32_t rel = static_cast<int32_t>(
            reinterpret_cast<uintptr_t>(stub) - (reinterpret_cast<uintptr_t>(hookAt) + 5));
        hookAt[0] = 0xE9;
        std::memcpy(hookAt + 1, &rel, 4);
        hookAt[5] = 0x90;  // NOP
        VirtualProtect(hookAt, 6, oldProt, &oldProt);
        FlushInstructionCache(GetCurrentProcess(), hookAt, 6);
        return true;
    }

    inline void RemoveHook()
    {
        if (!g_hookAt || !g_stub) return;
        auto* hookAt = reinterpret_cast<uint8_t*>(g_hookAt);
        DWORD oldProt = 0;
        VirtualProtect(hookAt, 6, PAGE_EXECUTE_READWRITE, &oldProt);
        constexpr uint8_t orig[6] = { 0x89, 0xB3, 0x88, 0x04, 0x00, 0x00 };
        std::memcpy(hookAt, orig, 6);
        VirtualProtect(hookAt, 6, oldProt, &oldProt);
        FlushInstructionCache(GetCurrentProcess(), hookAt, 6);
        VirtualFree(g_stub, 0, MEM_RELEASE);
        g_stub   = nullptr;
        g_hookAt = 0;
    }

    inline bool Initialize()
    {
        uint8_t* base = nullptr;
        size_t   size = 0;
        if (!Utils::GetModuleRange(L"DDS-Win64-Shipping.exe", base, size)) return false;
        uint8_t* hit = Utils::ScanPattern(base, size, k_Pattern, k_Mask, sizeof(k_Pattern));
        if (!hit) return false;
        return InstallHook(hit);
    }

    inline void Shutdown() { RemoveHook(); }

} // namespace LavenderHook::Memory::EnemiesMaxFallbackHook
