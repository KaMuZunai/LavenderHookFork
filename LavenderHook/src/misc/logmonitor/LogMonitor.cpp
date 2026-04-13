#include "LogMonitor.h"
#include "../xpsystem/XpTables.h"
#include "../xpsystem/XpManager.h"
#include "../Globals.h"
#include "../../ui/components/Console.h"

#include <thread>
#include <atomic>
#include <fstream>
#include <chrono>
#include <mutex>
#include <string>

using namespace std::chrono_literals;

namespace LavenderHook {
    namespace LogMonitor {

        // State
        static std::thread      s_thread;
        static std::atomic_bool s_running{ false };

        // File tracking
        static std::streamoff   s_fileOffset{ 0 };

        // True during the initial pass over existing log content on Start().
        static std::atomic_bool s_catchingUp{ false };

        // Session
        static std::mutex    s_sessionMutex;
        static SessionInfo   s_currentSession;
        static std::string   s_lastLoggedMapName;

        // Phase flags
        static std::atomic_bool s_inTavern{ false };
        static std::atomic_bool s_isCombatPhase{ false };
        static std::atomic_bool s_isBossWave{ false };
        static std::atomic_bool s_isPreSummaryPhase{ false };
        static std::atomic_bool s_combatAborted{ false };

        // Wave / build-phase counters
        static std::atomic<int> s_currentWave{ 0 };
        static std::atomic<int> s_lastEndedBuildPhase{ 0 };
        static std::atomic<int> s_lastSeenCombatStartWave{ 0 };
        static std::atomic<int> s_lastSeenCombatEndWave{ 0 };

        // Skip the first BuildPhase wave-advance when joining a session mid-game.
        static std::atomic_bool s_skipNextBuildPhase{ false };

        // After SummaryPhase_0 is aborted the game skips a BuildPhase number.
        static std::atomic_bool s_adjustNextBuildPhase{ false };

        // Abort
        static std::atomic_bool s_hasNewAbort{ false };
        static std::chrono::steady_clock::time_point s_lastAbortTime{};

        // Restart
        static std::atomic_bool s_hasJustRestarted{ false };

        // Internal helpers
        static std::string ResolveLogPath()
        {
            char buf[MAX_PATH];
            size_t len = 0;
            getenv_s(&len, buf, sizeof(buf), "LOCALAPPDATA");
            if (len == 0) return {};
            return std::string(buf) + "\\DDS\\Saved\\Logs\\DDS.log";
        }

        // Returns the trailing integer from a phase token
        static int ParsePhaseWave(const std::string& line,
            const std::string& prefix,
            const std::string& suffix)
        {
            const size_t ps = line.find(prefix);
            if (ps == std::string::npos) return -1;
            const size_t pe = line.find(suffix, ps);
            if (pe == std::string::npos) return -1;
            const std::string name = line.substr(ps, pe - ps);
            const size_t us = name.rfind('_');
            if (us == std::string::npos || us + 1 >= name.size()) return -1;
            try { return std::stoi(name.substr(us + 1)); }
            catch (...) { return -1; }
        }

        static SessionInfo ParseTryHostGameLine(const std::string& line)
        {
            SessionInfo info;

            const auto extractField = [&](const std::string& key) -> std::string {
                const std::string search = key + "=";
                size_t pos = line.find(search);
                if (pos == std::string::npos) return {};
                pos += search.size();
                size_t end = line.find_first_of(",)", pos);
                if (end == std::string::npos) end = line.size();
                std::string val = line.substr(pos, end - pos);
                while (!val.empty() && (val.back() == ' ' || val.back() == '\t'))
                    val.pop_back();
                return val;
                };

            const auto extractOpt = [&](const std::string& key) -> std::string {
                const std::string search = "?" + key + "=";
                size_t pos = line.find(search);
                if (pos == std::string::npos) return {};
                pos += search.size();
                size_t end = line.find_first_of("?,)", pos);
                if (end == std::string::npos) end = line.size();
                return line.substr(pos, end - pos);
                };

            const auto extractBoolOpt = [&](const std::string& key) -> bool {
                return extractOpt(key) == "true";
                };

            info.gameMode = extractField("GameMode");
            info.gameDifficulty = extractField("GameDifficulty");
            info.mapName = extractField("MapName");
            info.hardcore = extractBoolOpt("HARDCORE");
            info.riftMode = extractBoolOpt("RIFTMODE");
            info.bonusWave = extractOpt("BonusWave");

            const std::string waveStr = extractOpt("Wave");
            if (!waveStr.empty())
                try { info.startWave = std::stoi(waveStr); }
            catch (...) {}

            info.valid = !info.gameMode.empty() || !info.mapName.empty();
            return info;
        }

        // State resets
        static void ResetPhaseState()
        {
            s_isCombatPhase.store(false);
            s_isBossWave.store(false);
            s_isPreSummaryPhase.store(false);
            s_combatAborted.store(false);
            s_adjustNextBuildPhase.store(false);
            s_lastSeenCombatStartWave.store(0);
            s_lastSeenCombatEndWave.store(0);
            s_lastEndedBuildPhase.store(0);
            Globals::boss_phase_active.store(false);
            Globals::enemies_alive.store(0);
            Globals::enemies_max.store(0);
            Globals::boss_health_current.store(0.f);
            Globals::boss_health_max.store(0.f);
            Globals::wave_time.store(0);
        }

        static void ApplyNewSession(const SessionInfo& parsed)
        {
            { std::lock_guard<std::mutex> lock(s_sessionMutex); s_currentSession = parsed; }
            s_inTavern.store(false);
            s_currentWave.store(parsed.startWave > 0 ? parsed.startWave : 1);
            s_skipNextBuildPhase.store(parsed.startWave > 0);
            ResetPhaseState();
        }

        static void ApplyRestart()
        {
            SessionInfo session;
            { std::lock_guard<std::mutex> lock(s_sessionMutex); session = s_currentSession; }
            s_inTavern.store(false);
            s_currentWave.store(session.startWave > 0 ? session.startWave : 1);
            s_skipNextBuildPhase.store(session.startWave > 0);
            s_hasJustRestarted.store(true);
            ResetPhaseState();
        }

        static void ApplyTavern()
        {
            {
                std::lock_guard<std::mutex> lock(s_sessionMutex);
                s_currentSession = SessionInfo{};
                s_currentSession.mapName = "Tavern";
            }
            s_inTavern.store(true);
            s_currentWave.store(0);
            s_skipNextBuildPhase.store(false);
            ResetPhaseState();
        }

        // Core line processor
        static void ProcessLine(const std::string& line)
        {
            const bool catchup = s_catchingUp.load();

            //New session 
            if (line.find("LogOnline: TryHostGame") != std::string::npos)
            {
                SessionInfo parsed = ParseTryHostGameLine(line);
                ApplyNewSession(parsed);

                if (!catchup && parsed.valid && parsed.mapName != s_lastLoggedMapName)
                {
                    s_lastLoggedMapName = parsed.mapName;
                    try {
                        auto& con = LavenderConsole::GetInstance();
                        con.Log("[Session] Map: " + parsed.mapName);
                        con.Log("[Session] Mode: " + parsed.gameMode);
                        con.Log("[Session] Difficulty: " + parsed.gameDifficulty);
                        con.Log("[Session] Hardcore: " + std::string(parsed.hardcore ? "Yes" : "No"));
                        con.Log("[Session] Rift Mode: " + std::string(parsed.riftMode ? "Yes" : "No"));
                        const std::string eff = parsed.bonusWave.empty()
                            ? XpTables::GetDefaultBonusWave(parsed.mapName) : parsed.bonusWave;
                        con.Log("[Session] Bonus Wave: " + (eff.empty() ? "None" : eff));
                        con.Log("[Session] Wave: " + (parsed.startWave > 0
                            ? std::to_string(parsed.startWave) : "1"));
                    }
                    catch (...) {}
                }
                return;
            }

            // Map restart
            if (line.find("TravelTo(Restart,") != std::string::npos)
            {
                ApplyRestart();
                if (!catchup)
                {
                    try { LavenderConsole::GetInstance().Log("[Session] Map: Restarted"); }
                    catch (...) {}
                }
                return;
            }

            // Tavern map load
            if (line.find("/Game/DDS/Maps/") != std::string::npos
                && line.find("Tavern") != std::string::npos
                && line.find("BeginTearingDown") == std::string::npos)
            {
                ApplyTavern();
                if (!catchup && s_lastLoggedMapName != "Tavern")
                {
                    s_lastLoggedMapName = "Tavern";
                    try { LavenderConsole::GetInstance().Log("[Session] Map: Tavern"); }
                    catch (...) {}
                }
                return;
            }

            // SummaryPhase aborted
            if (line.find("SummaryPhase_") != std::string::npos
                && line.find("has been Aborted") != std::string::npos)
            {
                s_adjustNextBuildPhase.store(true);
                return;
            }

            // Combat phase aborted 
            if (line.find("CombatPhase_") != std::string::npos
                && line.find("has been Aborted") != std::string::npos)
            {
                s_combatAborted.store(true);
                s_isCombatPhase.store(false);
                if (!catchup && Globals::stop_on_fail)
                {
                    s_hasNewAbort.store(true);
                    s_lastAbortTime = std::chrono::steady_clock::now();
                    try { LavenderConsole::GetInstance().Log("LogMonitor: stopped actions"); }
                    catch (...) {}
                }
                return;
            }

            // Boss phase aborted
            if (line.find("BossPhase_") != std::string::npos
                && line.find("has been Aborted") != std::string::npos)
            {
                s_combatAborted.store(true);
                s_isCombatPhase.store(false);
                s_isBossWave.store(false);
                Globals::boss_phase_active.store(false);
                if (!catchup && Globals::stop_on_fail)
                {
                    s_hasNewAbort.store(true);
                    s_lastAbortTime = std::chrono::steady_clock::now();
                    try { LavenderConsole::GetInstance().Log("LogMonitor: stopped actions"); }
                    catch (...) {}
                }
                return;
            }

            // All remaining phase lines are irrelevant while in the Tavern
            if (s_inTavern.load()) return;

            // Non Tavern map load with explicit wave
            if (line.find("/Game/DDS/Maps/") != std::string::npos
                && line.find("?Wave=") != std::string::npos
                && line.find("Tavern") == std::string::npos)
            {
                const std::string key = "?Wave=";
                const size_t vs = line.find(key) + key.size();
                size_t ve = line.find_first_of("?)", vs);
                if (ve == std::string::npos) ve = line.size();
                try {
                    const int w = std::stoi(line.substr(vs, ve - vs));
                    if (w > 0) s_currentWave.store(w);
                }
                catch (...) {}
                return;
            }

            // Build phase started
            if (line.find("BuildPhase_") != std::string::npos
                && line.find(" has Started") != std::string::npos)
            {
                const int buildWave = ParsePhaseWave(line, "BuildPhase_", " has Started");
                if (buildWave > 0)
                {
                    s_isBossWave.store(false);
                    Globals::boss_phase_active.store(false);
                    s_isCombatPhase.store(false);
                    s_isPreSummaryPhase.store(false);
                    s_combatAborted.store(false);
                    s_lastSeenCombatStartWave.store(0);
                    s_lastSeenCombatEndWave.store(0);
                    if (s_skipNextBuildPhase.load())
                        s_skipNextBuildPhase.store(false);
                    else if (s_adjustNextBuildPhase.load())
                    {
                        s_adjustNextBuildPhase.store(false);
                        s_currentWave.store(buildWave);
                    }
                    else
                        s_currentWave.store(buildWave + 1);
                }
                return;
            }

            // Build phase ended
            if (line.find("BuildPhase_") != std::string::npos
                && line.find(" has Ended") != std::string::npos)
            {
                const int endedPhase = ParsePhaseWave(line, "BuildPhase_", " has Ended");
                if (endedPhase > 0)
                    s_lastEndedBuildPhase.store(endedPhase);
                return;
            }

            // Boss phase started 
            if ((line.find("BossCombatPhase_") != std::string::npos
                    || line.find("BossPhase_") != std::string::npos)
                && line.find(" has Started") != std::string::npos)
            {
                s_isBossWave.store(true);
                Globals::boss_phase_active.store(true);
                const int emax = Globals::enemies_max.load();
                if (emax > 0)
                {
                    const float hp = static_cast<float>(emax);
                    Globals::boss_health_current.store(hp);
                    Globals::boss_health_max.store(hp);
                }
                return;
            }

            // Combat phase started
            if (line.find("CombatPhase_") != std::string::npos
                && line.find(" has Started") != std::string::npos)
            {
                const int startWave = ParsePhaseWave(line, "CombatPhase_", " has Started");
                if (startWave > 0)
                {
                    s_lastSeenCombatStartWave.store(startWave);
                    s_isCombatPhase.store(true);
                    s_combatAborted.store(false);
                    s_isPreSummaryPhase.store(false);
                }
                return;
            }

            // Combat phase ended
            if (line.find("CombatPhase_") != std::string::npos
                && line.find(" has Ended") != std::string::npos)
            {
                const int endWave = ParsePhaseWave(line, "CombatPhase_", " has Ended");
                if (endWave > 0 && endWave > s_lastSeenCombatEndWave.load())
                {
                    s_lastSeenCombatEndWave.store(endWave);
                    if (endWave >= s_lastSeenCombatStartWave.load())
                        s_isCombatPhase.store(false);

                    if (!catchup)
                    {
                        SessionInfo session;
                        { std::lock_guard<std::mutex> lock(s_sessionMutex); session = s_currentSession; }
                        const std::string eff = session.bonusWave.empty()
                            ? XpTables::GetDefaultBonusWave(session.mapName) : session.bonusWave;
                        XpManager::AwardXp(XpTables::CalculateWaveXp(
                            session.mapName, eff, session.gameDifficulty,
                            session.gameMode, session.hardcore, session.riftMode, endWave));
                    }
                }
                return;
            }

            // Pre summary phase started
            if (line.find("PreSummaryPhase_") != std::string::npos
                && line.find(" has Started") != std::string::npos)
            {
                s_isPreSummaryPhase.store(true);
                s_isCombatPhase.store(false);
                return;
            }
        }

        // Background thread
        static void ThreadFunc()
        {
            const std::string logPath = ResolveLogPath();

            while (s_running.load())
            {
                try
                {
                    std::ifstream f(logPath, std::ios::in | std::ios::binary);
                    if (f)
                    {
                        f.seekg(0, std::ios::end);
                        const std::streamoff fileSize = f.tellg();

                        if (fileSize < s_fileOffset)
                        {
                            // Log was rotated or truncated
                            s_fileOffset = 0;
                            s_catchingUp.store(true);
                        }

                        if (fileSize > s_fileOffset)
                        {
                            f.seekg(s_fileOffset);

                            std::string line;
                            line.reserve(256);
                            char c;
                            while (f.get(c))
                            {
                                if (c == '\r') continue;
                                if (c == '\n')
                                {
                                    if (!line.empty())
                                    {
                                        ProcessLine(line);
                                        line.clear();
                                    }
                                }
                                else
                                {
                                    line.push_back(c);
                                }
                            }
                            // Partial line at EOF
                            s_fileOffset = fileSize;
                        }

                        // Initial catchup pass complete
                        if (s_catchingUp.load())
                            s_catchingUp.store(false);
                    }
                }
                catch (...) {}

                std::this_thread::sleep_for(20ms);
            }
        }

        // API
        void Start()
        {
            if (s_running.load()) return;
            XpManager::Init();
            s_fileOffset = 0;         // read from top
            s_catchingUp.store(true); 
            s_running.store(true);
            s_thread = std::thread(ThreadFunc);
        }

        void Stop()
        {
            if (!s_running.load()) return;
            s_running.store(false);
            if (s_thread.joinable()) s_thread.join();
        }

        bool LatestLineHasAbort()
        {
            if (!s_hasNewAbort.load()) return false;
            using namespace std::chrono;
            const auto now = steady_clock::now();
            if (s_lastAbortTime.time_since_epoch().count() == 0) return true;
            if ((now - s_lastAbortTime) <= milliseconds(1000)) return true;
            s_hasNewAbort.store(false);
            return false;
        }

        bool HasJustRestarted()
        {
            return s_hasJustRestarted.exchange(false);
        }

        SessionInfo GetCurrentSession()
        {
            std::lock_guard<std::mutex> lock(s_sessionMutex);
            return s_currentSession;
        }

        int  GetCurrentWave() { return s_currentWave.load(); }
        bool IsInTavern() { return s_inTavern.load(); }
        bool IsBossWave() { return s_isBossWave.load(); }
        bool IsCombatPhase() { return s_isCombatPhase.load(); }
        bool IsCombatAborted() { return s_combatAborted.load(); }
        bool IsPreSummaryPhase() { return s_isPreSummaryPhase.load(); }

        bool IsFinalBuildPhaseEnded()
        {
            const int maxWave = GetMaxWave();
            if (maxWave <= 0) return false;
            return s_lastEndedBuildPhase.load() == (maxWave - 1);
        }

        int GetMaxWave()
        {
            SessionInfo session;
            { std::lock_guard<std::mutex> lock(s_sessionMutex); session = s_currentSession; }
            const std::string& gm = session.gameMode;
            if (gm == "Gamemode.PureStrategy" ||
                gm == "Gamemode.Survival" ||
                gm == "Gamemode.Survival.Mixed")
                return 25;
            if (gm == "Gamemode.Campaign")
                return 5;
            return 0;
        }

    } // namespace LogMonitor
} // namespace LavenderHook
