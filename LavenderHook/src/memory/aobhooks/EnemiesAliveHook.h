#pragma once

#include <windows.h>
#include <cstdint>
#include <cstring>
#include "../aobutils/ScannerUtils.h"
#include "../../misc/Globals.h"

namespace LavenderHook::Memory::EnemiesAliveHook {

    // Alive Enemies Counter.

    // SUB DWORD PTR [RBX+0x26C]
    inline constexpr uint8_t k_Pattern[] = {
        0x83, 0xAB, 0x6C, 0x02, 0x00, 0x00, 0x01,  // SUB DWORD PTR [RBX+0x26C], 1
        0x75, 0x0E,                                   // JNZ +0x0E
        0x48, 0x8B, 0x43, 0xD8,                       // MOV RAX, [RBX-0x28]
        0x48, 0x8D, 0x4B, 0xD8,                       // LEA RCX, [RBX-0x28]
        0xFF, 0x90, 0x80, 0x02, 0x00, 0x00,           // CALL QWORD PTR [RAX+0x280]
        0x48, 0x8B, 0x74, 0x24, 0x38,                 // MOV RSI, [RSP+0x38]
        0x48, 0x8B, 0x5C, 0x24, 0x40,                 // MOV RBX, [RSP+0x40]
        0x48, 0x83, 0xC4, 0x20                         // ADD RSP, 0x20
    };

    inline void*     g_stub   = nullptr;
    inline uintptr_t g_hookAt = 0;

    inline bool InstallHook(uint8_t* hookAt)
    {
        void* stub = Utils::AllocNear(reinterpret_cast<uintptr_t>(hookAt), 128);
        if (!stub) return false;

        const uintptr_t aliveAddr = reinterpret_cast<uintptr_t>(&Globals::enemies_alive);
        const uintptr_t maxAddr   = reinterpret_cast<uintptr_t>(&Globals::enemies_max);
        const uintptr_t retAddr   = reinterpret_cast<uintptr_t>(hookAt) + 7;

        uint8_t* p = static_cast<uint8_t*>(stub);

        // Original instruction
        *p++ = 0x83; *p++ = 0xAB;
        *p++ = 0x6C; *p++ = 0x02; *p++ = 0x00; *p++ = 0x00; *p++ = 0x01;

        *p++ = 0x50;                                          // PUSH RAX
        *p++ = 0x8B; *p++ = 0x83;                            // MOV EAX, [RBX+0x26C]
        *p++ = 0x6C; *p++ = 0x02; *p++ = 0x00; *p++ = 0x00;
        *p++ = 0xA3; std::memcpy(p, &aliveAddr, 8); p += 8;  // MOV [enemies_alive], EAX
        *p++ = 0x85; *p++ = 0xC0;                            // TEST EAX, EAX
        *p++ = 0x75; *p++ = 0x09;                            // JNZ +9  (skip max reset)
        *p++ = 0xA3; std::memcpy(p, &maxAddr, 8); p += 8;   // MOV [enemies_max], EAX (=0)
        *p++ = 0x58;                                          // POP RAX
        *p++ = 0xFF; *p++ = 0x25;                            // JMP [RIP+0]
        *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
        std::memcpy(p, &retAddr, 8); p += 8;

        g_stub   = stub;
        g_hookAt = reinterpret_cast<uintptr_t>(hookAt);

        DWORD oldProt = 0;
        VirtualProtect(hookAt, 7, PAGE_EXECUTE_READWRITE, &oldProt);
        const int32_t rel = static_cast<int32_t>(
            reinterpret_cast<uintptr_t>(stub) - (reinterpret_cast<uintptr_t>(hookAt) + 5));
        hookAt[0] = 0xE9;
        std::memcpy(hookAt + 1, &rel, 4);
        hookAt[5] = 0x90;  // NOP
        hookAt[6] = 0x90;  // NOP
        VirtualProtect(hookAt, 7, oldProt, &oldProt);
        FlushInstructionCache(GetCurrentProcess(), hookAt, 7);
        return true;
    }

    inline void RemoveHook()
    {
        if (!g_hookAt || !g_stub) return;
        auto* hookAt = reinterpret_cast<uint8_t*>(g_hookAt);
        DWORD oldProt = 0;
        VirtualProtect(hookAt, 7, PAGE_EXECUTE_READWRITE, &oldProt);
        constexpr uint8_t orig[7] = {0x83, 0xAB, 0x6C, 0x02, 0x00, 0x00, 0x01};
        std::memcpy(hookAt, orig, 7);
        VirtualProtect(hookAt, 7, oldProt, &oldProt);
        FlushInstructionCache(GetCurrentProcess(), hookAt, 7);
        VirtualFree(g_stub, 0, MEM_RELEASE);
        g_stub   = nullptr;
        g_hookAt = 0;
    }

    inline bool Initialize()
    {
        uint8_t* base = nullptr;
        size_t   size = 0;
        if (!Utils::GetModuleRange(L"DDS-Win64-Shipping.exe", base, size)) return false;
        uint8_t* hit = Utils::ScanPattern(base, size, k_Pattern, sizeof(k_Pattern));
        if (!hit) return false;
        return InstallHook(hit);
    }

    inline void Shutdown() { RemoveHook(); }

} // namespace LavenderHook::Memory::EnemiesAliveHook
