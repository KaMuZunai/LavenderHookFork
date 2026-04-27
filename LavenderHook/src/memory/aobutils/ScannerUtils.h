#pragma once

#include <windows.h>
#include <cstdint>
#include <cstring>

namespace LavenderHook::Memory::Utils {

    // Resolve the base address and mapped size of a loaded module.
    inline bool GetModuleRange(const wchar_t* name, uint8_t*& base, size_t& outSize)
    {
        HMODULE h = GetModuleHandleW(name);
        if (!h) return false;
        base = reinterpret_cast<uint8_t*>(h);
        const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
        outSize = static_cast<size_t>(nt->OptionalHeader.SizeOfImage);
        return true;
    }

    // Scan of a memory range for an exact byte pattern.
    inline uint8_t* ScanPattern(uint8_t* base, size_t len,
                                  const uint8_t* pat, size_t patLen)
    {
        if (len < patLen) return nullptr;
        for (size_t i = 0, e = len - patLen; i <= e; ++i)
            if (std::memcmp(base + i, pat, patLen) == 0)
                return base + i;
        return nullptr;
    }

    inline uint8_t* ScanPattern(uint8_t* base, size_t len,
                                  const uint8_t* pat, const uint8_t* mask, size_t patLen)
    {
        if (len < patLen) return nullptr;
        for (size_t i = 0, e = len - patLen; i <= e; ++i)
        {
            bool found = true;
            for (size_t j = 0; j < patLen; ++j)
            {
                if (mask[j] && base[i + j] != pat[j])
                {
                    found = false;
                    break;
                }
            }
            if (found) return base + i;
        }
        return nullptr;
    }

    // Allocate PAGE_EXECUTE_READWRITE memory within ±1 GB of the target address.
    inline void* AllocNear(uintptr_t target, size_t size)
    {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        const uintptr_t gran  = si.dwAllocationGranularity;
        const uintptr_t range = 0x40000000ULL; // 1 GB

        const uintptr_t lo = (target > range) ? (target - range) : 0;
        const uintptr_t hi = target + range;

        MEMORY_BASIC_INFORMATION mbi{};

        // Search backward
        uintptr_t probe = target & ~(gran - 1ULL);
        while (probe > lo)
        {
            if (VirtualQuery(reinterpret_cast<LPCVOID>(probe), &mbi, sizeof(mbi)) == 0) break;
            const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            if (mbi.State == MEM_FREE)
            {
                const uintptr_t aligned = (base + gran - 1) & ~(gran - 1ULL);
                if (aligned >= lo && aligned + size <= base + mbi.RegionSize)
                {
                    void* m = VirtualAlloc(reinterpret_cast<void*>(aligned), size,
                                           MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
                    if (m) return m;
                }
            }
            if (base == 0) break;
            probe = base - 1;
        }

        // Search forward
        probe = (target & ~(gran - 1ULL)) + gran;
        while (probe < hi)
        {
            if (VirtualQuery(reinterpret_cast<LPCVOID>(probe), &mbi, sizeof(mbi)) == 0) break;
            const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            if (mbi.State == MEM_FREE)
            {
                const uintptr_t aligned = (base + gran - 1) & ~(gran - 1ULL);
                if (aligned + size <= base + mbi.RegionSize && aligned < hi)
                {
                    void* m = VirtualAlloc(reinterpret_cast<void*>(aligned), size,
                                           MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
                    if (m) return m;
                }
            }
            probe = base + mbi.RegionSize;
        }

        return nullptr;
    }

} // namespace LavenderHook::Memory::Utils
