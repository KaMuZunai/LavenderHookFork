#pragma once

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include "../aobutils/ScannerUtils.h"
#include "../../misc/Globals.h"
#include "../../ui/components/Console.h"

namespace LavenderHook::Memory::TimeHook {

    // Timer Hook.

    // MOV [RDI], EAX
    inline constexpr uint8_t k_Pattern[] = {
        0x89, 0x07,                                              // MOV [RDI], EAX
        0x48, 0x83, 0xC4, 0x20,                                  // ADD RSP, 0x20
        0x5F,                                                    // POP RDI
        0xC3,                                                    // RET
        0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,         // INT3 padding
        0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,               // (15 bytes total)
        0x48, 0x89, 0x5C, 0x24, 0x08,                           // MOV [RSP+0x08], RBX
        0x57,                                                    // PUSH RDI
        0x48, 0x83, 0xEC, 0x20,                                  // SUB RSP, 0x20
        0x48, 0x83, 0x7A, 0x20, 0x00                            // CMP QWORD PTR [RDX+0x20], 0
    };

    inline void*     g_stub   = nullptr;
    inline uintptr_t g_hookAt = 0;

    inline void*     g_stub2   = nullptr;
    inline uintptr_t g_hookAt2 = 0;

    // Short pattern
    inline constexpr uint8_t k_CorePattern[] = {
        0x89, 0x07,              // MOV [RDI], EAX
        0x48, 0x83, 0xC4, 0x20,  // ADD RSP, 0x20
        0x5F,                    // POP RDI
        0xC3                     // RET
    };

    inline int g_lastTime = -1;

    inline bool InstallHookAt(uint8_t* hookAt, void*& outStub, uintptr_t& outHookAt)
    {
        void* stub = Utils::AllocNear(reinterpret_cast<uintptr_t>(hookAt), 64);
        if (!stub) return false;

        const uintptr_t timeAddr = reinterpret_cast<uintptr_t>(&Globals::wave_time);
        const uintptr_t retAddr  = reinterpret_cast<uintptr_t>(hookAt) + 6;

        uint8_t* p = static_cast<uint8_t*>(stub);

        // [+00] Original MOV [RDI], EAX
        *p++ = 0x89; *p++ = 0x07;

        // [+02] PUSH RAX
        *p++ = 0x50;

        // [+03] MOV EAX, [RDI] re-read the committed value from memory
        *p++ = 0x8B; *p++ = 0x07;

        // [+05] MOV [wave_time], EAX direct absolute store (A3 moffs64)
        *p++ = 0xA3; std::memcpy(p, &timeAddr, 8); p += 8;

        // [+0E] POP RAX
        *p++ = 0x58;

        // [+0F] Original ADD RSP, 0x20
        *p++ = 0x48; *p++ = 0x83; *p++ = 0xC4; *p++ = 0x20;

        // [+13] JMP [RIP+0] -> retAddr (hookAt+6 = POP RDI)
        *p++ = 0xFF; *p++ = 0x25;
        *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
        std::memcpy(p, &retAddr, 8); p += 8;

        outStub   = stub;
        outHookAt = reinterpret_cast<uintptr_t>(hookAt);

        DWORD oldProt = 0;
        VirtualProtect(hookAt, 6, PAGE_EXECUTE_READWRITE, &oldProt);
        const int32_t rel = static_cast<int32_t>(
            reinterpret_cast<uintptr_t>(stub) - (reinterpret_cast<uintptr_t>(hookAt) + 5));
        hookAt[0] = 0xE9;
        std::memcpy(hookAt + 1, &rel, 4);
        hookAt[5] = 0x90;
        VirtualProtect(hookAt, 6, oldProt, &oldProt);
        FlushInstructionCache(GetCurrentProcess(), hookAt, 6);
        return true;
    }

    inline void Tick()
    {
        const int cur = Globals::wave_time.load();
        if (cur == g_lastTime) return;
        g_lastTime = cur;
        if (cur > 0)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "[TimeHook] Wave time: %d:%02d", cur / 60, cur % 60);
            LavenderConsole::GetInstance().Log(buf);
        }
    }

    inline bool InstallHook(uint8_t* hookAt)
    {
        return InstallHookAt(hookAt, g_stub, g_hookAt);
    }

    inline void RemoveHook()
    {
        constexpr uint8_t orig[6] = {0x89, 0x07, 0x48, 0x83, 0xC4, 0x20};
        auto removeOne = [&](uintptr_t& hookAt, void*& stub) {
            if (!hookAt || !stub) return;
            auto* p = reinterpret_cast<uint8_t*>(hookAt);
            DWORD oldProt = 0;
            VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &oldProt);
            std::memcpy(p, orig, 6);
            VirtualProtect(p, 6, oldProt, &oldProt);
            FlushInstructionCache(GetCurrentProcess(), p, 6);
            VirtualFree(stub, 0, MEM_RELEASE);
            stub   = nullptr;
            hookAt = 0;
        };
        removeOne(g_hookAt,  g_stub);
        removeOne(g_hookAt2, g_stub2);
    }

    inline bool Initialize()
    {
        uint8_t* base = nullptr;
        size_t   size = 0;

        if (!Utils::GetModuleRange(L"DDS-Win64-Shipping.exe", base, size)) return false;

        uint8_t* hit = Utils::ScanPattern(base, size, k_Pattern, sizeof(k_Pattern));
        if (!hit) return false;

        if (!InstallHookAt(hit, g_stub, g_hookAt)) return false;

        uint8_t* searchBase = hit + 2;
        const size_t remaining = static_cast<size_t>((base + size) - searchBase);
        const size_t searchLen = remaining < 2048 ? remaining : 2048;
        uint8_t* hit2 = Utils::ScanPattern(searchBase, searchLen,
                                           k_CorePattern, sizeof(k_CorePattern));
        if (hit2)
            InstallHookAt(hit2, g_stub2, g_hookAt2);

        return true;
    }

    inline void Shutdown() { RemoveHook(); }

} // namespace LavenderHook::Memory::TimeHook
