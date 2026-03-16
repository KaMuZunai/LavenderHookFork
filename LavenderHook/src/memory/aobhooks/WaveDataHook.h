#pragma once

#include <windows.h>
#include <cstdint>
#include <cstring>
#include "../aobutils/ScannerUtils.h"
#include "../../misc/Globals.h"

namespace LavenderHook::Memory::WaveDataHook {

    // Max Enemies for wave and Boss Health.

    // MOVSS [RCX+0x04], XMM2
    inline constexpr uint8_t k_Pattern[] = {
        0xF3, 0x0F, 0x11, 0x51, 0x04,              // MOVSS [RCX+0x04], XMM2
        0x8B, 0x87, 0x40, 0x04, 0x00, 0x00,        // MOV EAX, [RDI+0x440]
        0x3B, 0x87, 0x6C, 0x04, 0x00, 0x00,        // CMP EAX, [RDI+0x46C]
        0x74, 0x4B,                                 // JE +0x4B
        0x48, 0x63, 0x8F, 0x80, 0x04, 0x00, 0x00,  // MOVSXD RCX, [RDI+0x480]
        0x4C, 0x8D, 0x87, 0x70, 0x04, 0x00, 0x00,  // LEA R8, [RDI+0x470]
        0x49, 0x8B, 0x50, 0x08                      // MOV RDX, [R8+0x08]
    };

    inline void*     g_stub   = nullptr;
    inline uintptr_t g_hookAt = 0;
    inline bool InstallHook(uint8_t* hookAt)
    {
        void* stub = Utils::AllocNear(reinterpret_cast<uintptr_t>(hookAt), 128);
        if (!stub) return false;

        const uintptr_t bossPhaseAddr  = reinterpret_cast<uintptr_t>(&Globals::boss_phase_active);
        const uintptr_t bossHpCurAddr  = reinterpret_cast<uintptr_t>(&Globals::boss_health_current);
        const uintptr_t bossHpMaxAddr  = reinterpret_cast<uintptr_t>(&Globals::boss_health_max);
        const uintptr_t enemiesMaxAddr = reinterpret_cast<uintptr_t>(&Globals::enemies_max);
        const uintptr_t retAddr        = reinterpret_cast<uintptr_t>(hookAt) + 5;

        uint8_t* p = static_cast<uint8_t*>(stub);

        // [+00] Original MOVSS [RCX+0x04], XMM2 (5)
        *p++ = 0xF3; *p++ = 0x0F; *p++ = 0x11; *p++ = 0x51; *p++ = 0x04;

        // [+05] Save RAX and RCX
        *p++ = 0x50;                                                   // PUSH RAX
        *p++ = 0x51;                                                   // PUSH RCX

        // [+07] Read Globals::boss_phase_active
        *p++ = 0x48; *p++ = 0xB8;                                     // MOV RAX, imm64
        std::memcpy(p, &bossPhaseAddr, 8); p += 8;
        *p++ = 0x0F; *p++ = 0xB6; *p++ = 0x00;                       // MOVZX EAX, [RAX]
        *p++ = 0x85; *p++ = 0xC0;                                     // TEST EAX, EAX
        *p++ = 0x74; *p++ = 0x22;                                     // JZ +0x22 → .normal

        // [+18] BOSS PHASE
        *p++ = 0x66; *p++ = 0x0F; *p++ = 0x7E; *p++ = 0xD0;         // MOVD EAX, XMM2
        *p++ = 0x48; *p++ = 0xB9;                                     // MOV RCX, imm64
        std::memcpy(p, &bossHpCurAddr, 8); p += 8;
        *p++ = 0x89; *p++ = 0x01;                                     // MOV [RCX], EAX
        *p++ = 0x48; *p++ = 0xB9;                                     // MOV RCX, imm64
        std::memcpy(p, &bossHpMaxAddr, 8); p += 8;
        *p++ = 0x3B; *p++ = 0x01;                                     // CMP EAX, [RCX]
        *p++ = 0x7E; *p++ = 0x02;                                     // JLE +2 → +38
        *p++ = 0x89; *p++ = 0x01;                                     // MOV [RCX], EAX
        *p++ = 0xEB; *p++ = 0x10;                                     // JMP +0x10 → .end

        // [+3A] NORMAL WAVE 
        *p++ = 0xF3; *p++ = 0x0F; *p++ = 0x2C; *p++ = 0xC2;         // CVTTSS2SI EAX, XMM2
        *p++ = 0x48; *p++ = 0xB9;                                     // MOV RCX, imm64
        std::memcpy(p, &enemiesMaxAddr, 8); p += 8;
        *p++ = 0x89; *p++ = 0x01;                                     // MOV [RCX], EAX

        // [+4A] END 
        *p++ = 0x59;                                                   // POP RCX
        *p++ = 0x58;                                                   // POP RAX
        *p++ = 0xFF; *p++ = 0x25;                                     // JMP [RIP+0]
        *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
        std::memcpy(p, &retAddr, 8); p += 8;

        g_stub   = stub;
        g_hookAt = reinterpret_cast<uintptr_t>(hookAt);

        // Patch original 5 bytes: E9 <rel32>
        DWORD oldProt = 0;
        VirtualProtect(hookAt, 5, PAGE_EXECUTE_READWRITE, &oldProt);
        const int32_t rel = static_cast<int32_t>(
            reinterpret_cast<uintptr_t>(stub) - (reinterpret_cast<uintptr_t>(hookAt) + 5));
        hookAt[0] = 0xE9;
        std::memcpy(hookAt + 1, &rel, 4);
        VirtualProtect(hookAt, 5, oldProt, &oldProt);
        FlushInstructionCache(GetCurrentProcess(), hookAt, 5);
        return true;
    }

    inline void RemoveHook()
    {
        if (!g_hookAt || !g_stub) return;
        auto* hookAt = reinterpret_cast<uint8_t*>(g_hookAt);
        DWORD oldProt = 0;
        VirtualProtect(hookAt, 5, PAGE_EXECUTE_READWRITE, &oldProt);
        constexpr uint8_t orig[5] = {0xF3, 0x0F, 0x11, 0x51, 0x04};
        std::memcpy(hookAt, orig, 5);
        VirtualProtect(hookAt, 5, oldProt, &oldProt);
        FlushInstructionCache(GetCurrentProcess(), hookAt, 5);
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

} // namespace LavenderHook::Memory::WaveDataHook
