#pragma once

#include <string>
#include <unordered_map>

namespace LavenderHook {
    namespace XpTables {

        // Difficulty multipliers
        inline const std::unordered_map<std::string, float> DifficultyMultipliers =
        {
            { "Difficulty.Easy",       1.00f  },
            { "Difficulty.Normal",     1.50f  },
            { "Difficulty.Hard",       2.75f  },
            { "Difficulty.Insane",     5.50f  },
            { "Difficulty.Nightmare",  12.00f },
            { "Difficulty.Massacre",   25.00f },
        };

        // Map Multipliers
        inline const std::unordered_map<std::string, float> MapMultipliers =
        {
            // No Bonus Difficulty Maps
            { "Map.TheDeeperWell",          0.60f },
            { "Map.AncientMines",           0.80f },
            { "Map.LavaMines",              1.10f },

            // Demon Lord Bonus Difficulty Maps
            { "Map.AlchemicalLaboratory",   1.70f },
            { "Map.MagusQuarters",          1.00f },
            { "Map.TornadoValley",          1.00f },
            { "Map.TornadoHighLands",       1.30f },
            { "Map.TheRamparts",            0.95f },

            // Goblin Mech Bonus Difficulty Maps
            { "Map.TheThroneRoom",          1.65f },
            { "Map.EndlessSpires",          1.40f },
            { "Map.ArcaneLibrary",          1.05f },
            { "Map.RoyalGardens",           1.20f },
            { "Map.ThePromenade",           1.15f },
            { "Map.VikingWinter",           1.20f },
            { "Map.HalloweenRamparts",      1.35f },

            // Ancient Dragon Bonus Difficulty Maps
            { "Map.TheSummit",              1.90f },
            { "Map.Glitterhelm",            1.40f },
            { "Map.TheMill",                1.15f },
            { "Map.TheOutpost",             1.15f },
            { "Map.FoundriesAndForges",     0.85f },

            // Lycan King Bonus Difficulty Maps
            { "Map.TheKeep",                2.20f },
            { "Map.TheBazaar",              0.90f },
            { "Map.TheLostMetropolis",      1.10f },
            { "Map.CastleArmory",           0.85f },
            { "Map.KingsGame",              1.30f },

            // Dune Eater Bonus Difficulty Maps
            { "Map.ForsakenTemple",         2.00f },
        };

        // Gamemode Multipliers
        inline const std::unordered_map<std::string, float> GameModeMultipliers =
        {
            { "Gamemode.NUE",               2.00f },
            { "Gamemode.Campaign",          2.00f },
            { "Gamemode.Survival",          0.50f },
            { "Gamemode.Survival.Mixed",    4.00f },
            { "Gamemode.PureStrategy",      5.00f },
            { "Gamemode.Challenge",         6.00f },
            { "Gamemode.BossTest",          10.00f },
        };

        // Bonus Difficulty Multipliers
        inline const std::unordered_map<std::string, float> BonusWaveMultipliers =
        {
            { "BonusWave.DemonLord",     1.30f },
            { "BonusWave.GoblinMech",    1.70f },
            { "BonusWave.AncientDragon", 2.40f },
            { "BonusWave.LycanKing",     4.80f },
            { "BonusWave.DuneEater",     7.00f },
        };

        // Default bonus wave per map
        inline const std::unordered_map<std::string, std::string> MapDefaultBonusWave =
        {
			// Demon Lord Bonus Difficulty Maps
            { "Map.AlchemicalLaboratory",  "BonusWave.DemonLord"     },
            { "Map.MagusQuarters",         "BonusWave.DemonLord"     },
            { "Map.TornadoValley",         "BonusWave.DemonLord"     },
            { "Map.TornadoHighLands",      "BonusWave.DemonLord"     },
            { "Map.TheRamparts",           "BonusWave.DemonLord"     },

			// Goblin Mech Bonus Difficulty Maps
            { "Map.TheThroneRoom",         "BonusWave.GoblinMech"    },
            { "Map.EndlessSpires",         "BonusWave.GoblinMech"    },
            { "Map.ArcaneLibrary",         "BonusWave.GoblinMech"    },
            { "Map.RoyalGardens",          "BonusWave.GoblinMech"    },
            { "Map.ThePromenade",          "BonusWave.GoblinMech"    },
            { "Map.VikingWinter",          "BonusWave.GoblinMech"    },
            { "Map.HalloweenRamparts",     "BonusWave.GoblinMech"    },

			// Ancient Dragon Bonus Difficulty Maps
            { "Map.TheSummit",             "BonusWave.AncientDragon" },
            { "Map.Glitterhelm",           "BonusWave.AncientDragon" },
            { "Map.TheMill",               "BonusWave.AncientDragon" },
            { "Map.TheOutpost",            "BonusWave.AncientDragon" },
            { "Map.FoundriesAndForges",    "BonusWave.AncientDragon" },

			// Lycan King Bonus Difficulty Maps
            { "Map.TheKeep",               "BonusWave.LycanKing"     },
            { "Map.TheBazaar",             "BonusWave.LycanKing"     },
            { "Map.TheLostMetropolis",     "BonusWave.LycanKing"     },
            { "Map.CastleArmory",          "BonusWave.LycanKing"     },
            { "Map.KingsGame",             "BonusWave.LycanKing"     },

			// Dune Eater Bonus Difficulty Maps
            { "Map.ForsakenTemple",        "BonusWave.DuneEater"     },
        };

        // Hardcore and Rifted bonuses
        inline constexpr float HardcoreBonus = 0.25f;
        inline constexpr float RiftModeBonus = 0.25f;
        inline constexpr float DefaultMultiplier = 1.00f;

        // helpers
        inline float GetMapMultiplier(const std::string& key)
        {
            auto it = MapMultipliers.find(key);
            return it != MapMultipliers.end() ? it->second : DefaultMultiplier;
        }

        inline float GetGameModeMultiplier(const std::string& key)
        {
            auto it = GameModeMultipliers.find(key);
            return it != GameModeMultipliers.end() ? it->second : DefaultMultiplier;
        }

        inline float GetDifficultyMultiplier(const std::string& key)
        {
            auto it = DifficultyMultipliers.find(key);
            return it != DifficultyMultipliers.end() ? it->second : DefaultMultiplier;
        }

        inline float GetBonusWaveMultiplier(const std::string& key)
        {
            if (key.empty()) return DefaultMultiplier;
            auto it = BonusWaveMultipliers.find(key);
            return it != BonusWaveMultipliers.end() ? it->second : DefaultMultiplier;
        }

        inline std::string GetDefaultBonusWave(const std::string& mapKey)
        {
            auto it = MapDefaultBonusWave.find(mapKey);
            return it != MapDefaultBonusWave.end() ? it->second : std::string{};
        }

        // XP threshold
        inline float GetXpThreshold(int level)
        {
            const float lf = static_cast<float>(level);
            return 100.f * (0.95f + lf * 0.05f) * (1.f + (lf * lf) / 100.f);
        }

        // Wave XP
        inline float CalculateWaveXp(
            const std::string& mapName,
            const std::string& bonusWave,
            const std::string& difficulty,
            const std::string& gameMode,
            bool  hardcore,
            bool  riftMode,
            int   wave)
        {
            const float mapMult = GetMapMultiplier(mapName);
            const float bwMult = GetBonusWaveMultiplier(bonusWave);
            const float hcVal = hardcore ? HardcoreBonus : 0.f;
            const float rmVal = riftMode ? RiftModeBonus : 0.f;
            const float diffMult = GetDifficultyMultiplier(difficulty);
            const float modeMult = GetGameModeMultiplier(gameMode);
            const float w = static_cast<float>(wave);
            const float waveFac = 1.f + (w * 0.01f) * (w * 0.01f) + w * 0.02f;

            return (mapMult * bwMult) * (1.f + hcVal + rmVal) * (diffMult * modeMult) * waveFac;
        }

    } // namespace XpTables
} // namespace LavenderHook
