#pragma once

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <atomic>
#include "../aobutils/ScannerUtils.h"
#include "../../misc/Globals.h"

namespace LavenderHook::Memory::ManaHook {

    // Captures the UEngine pointer (RCX) from a hot engine vtable dispatch site,
    // then on each Tick() traverses the pointer chain to read mana values from the
    // player's AttributeSet and stores them in Globals::mana_current / mana_max.
    //
    // Pointer chain (from CE Lua reference):
    //   engine    = captured RCX
    //   viewport  = *( engine    + 0x788 )
    //   world     = *( viewport  + 0x078 )
    //   gamestate = *( world     + 0x130 )
    //   parray    = *( gamestate + 0x238 )
    //   p1        = *( parray    + 0x000 )
    //   owner     = *( p1        + 0x0E0 )
    //   character = *( owner     + 0x260 )
    //   attrset   = *( character + 0x718 )
    //   mana_current = *float( attrset + 0x188 )
    //   mana_max     = *float( attrset + 0x198 )

    inline constexpr uint32_t k_ManaCurrentOffset = 0x188;
    inline constexpr uint32_t k_ManaMaxOffset      = 0x198;

    // AOB: MOV RAX,[RCX]  /  MOV RBX,RCX  followed by unique context bytes.
    // We hook the first 6 bytes and trampoline back after capturing RCX.
    inline constexpr uint8_t k_Pattern[] = {
        0x48, 0x8B, 0x01,                    // MOV RAX, [RCX]
        0x48, 0x8B, 0xD9,                    // MOV RBX, RCX
        0x0F, 0x29, 0x74, 0x24, 0x30,       // MOVAPS [RSP+0x30], XMM6
        0x0F, 0x28, 0xF1,                    // MOVAPS XMM6, XMM1
        0xFF, 0x90                            // CALL QWORD PTR [RAX+...] (partial, for uniqueness)
    };

    inline void*                  g_stub      = nullptr;
    inline uintptr_t              g_hookAt    = 0;
    inline std::atomic<uintptr_t> g_enginePtr { 0 };

    inline bool InstallHook(uint8_t* hookAt)
    {
        void* stub = Utils::AllocNear(reinterpret_cast<uintptr_t>(hookAt), 64);
        if (!stub) return false;

        const uintptr_t engPtrAddr = reinterpret_cast<uintptr_t>(&g_enginePtr);
        const uintptr_t retAddr    = reinterpret_cast<uintptr_t>(hookAt) + 6;

        uint8_t* p = static_cast<uint8_t*>(stub);

        // [+00] Original: MOV RAX, [RCX]  (3 bytes)
        *p++ = 0x48; *p++ = 0x8B; *p++ = 0x01;

        // [+03] Original: MOV RBX, RCX    (3 bytes)
        *p++ = 0x48; *p++ = 0x8B; *p++ = 0xD9;

        // [+06] PUSH RAX  — preserve RAX before we clobber it
        *p++ = 0x50;

        // [+07] MOV RAX, imm64  (address of g_enginePtr)
        *p++ = 0x48; *p++ = 0xB8;
        std::memcpy(p, &engPtrAddr, 8); p += 8;

        // [+11] MOV [RAX], RCX  — store engine pointer (atomic store via plain write; aligned qword)
        *p++ = 0x48; *p++ = 0x89; *p++ = 0x08;

        // [+14] POP RAX
        *p++ = 0x58;

        // [+15] JMP [RIP+0] -> retAddr  (absolute indirect far jump)
        *p++ = 0xFF; *p++ = 0x25;
        *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
        std::memcpy(p, &retAddr, 8); p += 8;

        g_stub   = stub;
        g_hookAt = reinterpret_cast<uintptr_t>(hookAt);

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

    inline void RemoveHook()
    {
        if (!g_hookAt || !g_stub) return;
        auto* hookAt = reinterpret_cast<uint8_t*>(g_hookAt);
        DWORD oldProt = 0;
        VirtualProtect(hookAt, 6, PAGE_EXECUTE_READWRITE, &oldProt);
        constexpr uint8_t orig[6] = { 0x48, 0x8B, 0x01, 0x48, 0x8B, 0xD9 };
        std::memcpy(hookAt, orig, 6);
        VirtualProtect(hookAt, 6, oldProt, &oldProt);
        FlushInstructionCache(GetCurrentProcess(), hookAt, 6);
        VirtualFree(g_stub, 0, MEM_RELEASE);
        g_stub   = nullptr;
        g_hookAt = 0;
    }

    // Traverses the pointer chain from the captured engine pointer and writes
    // current and max mana into Globals::mana_current / Globals::mana_max.
    // Call once per frame. Any null intermediate pointer aborts without updating.
    inline void Tick()
    {
        const uintptr_t engine = g_enginePtr.load(std::memory_order_relaxed);
        if (!engine) return;

        auto rd = [](uintptr_t base, uint32_t off) -> uintptr_t {
            return *reinterpret_cast<uintptr_t*>(base + off);
        };

        const uintptr_t viewport  = rd(engine,    0x788); if (!viewport)  return;
        const uintptr_t world     = rd(viewport,  0x078); if (!world)     return;
        const uintptr_t gamestate = rd(world,     0x130); if (!gamestate) return;
        const uintptr_t parray    = rd(gamestate, 0x238); if (!parray)    return;
        const uintptr_t p1        = rd(parray,    0x000); if (!p1)        return;
        const uintptr_t owner     = rd(p1,        0x0E0); if (!owner)     return;
        const uintptr_t character = rd(owner,     0x260); if (!character) return;
        const uintptr_t attrset   = rd(character, 0x718); if (!attrset)   return;

        Globals::mana_current.store(
            *reinterpret_cast<const float*>(attrset + k_ManaCurrentOffset),
            std::memory_order_relaxed);

        Globals::mana_max.store(
            *reinterpret_cast<const float*>(attrset + k_ManaMaxOffset),
            std::memory_order_relaxed);
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

} // namespace LavenderHook::Memory::ManaHook
