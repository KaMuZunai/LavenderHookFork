#include "LogMonitor.h"
#include "XpTables.h"
#include "XpManager.h"
#include "Globals.h"
#include "../ui/components/Console.h"

#include <thread>
#include <atomic>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <mutex>

using namespace std::chrono_literals;

namespace LavenderHook {
	namespace LogMonitor {

		static std::thread s_thread;
		static std::atomic_bool s_running{ false };

		// abort detection
		static std::atomic_bool s_hasNewAbort{ false };
		static std::string s_lastAbortLine;
		static std::chrono::steady_clock::time_point s_lastAbortTime{};

		// session detection
		static std::mutex   s_sessionMutex;
		static SessionInfo  s_currentSession;
		static std::string  s_lastTryHostGameLine;

		// wave / XP detection
		static std::string  s_lastCombatPhaseLine;

		// Resolve the DDS log path once
		static std::string ResolveLogPath()
		{
			const char* pathTemplate = "C:\\Users\\%USERNAME%\\AppData\\Local\\DDS\\Saved\\Logs\\DDS.log";

			char username[256]; size_t outlen = 0;
			getenv_s(&outlen, username, sizeof(username), "USERNAME");

			std::string path(pathTemplate);
			size_t pos = path.find("%USERNAME%");
			if (pos != std::string::npos) path.replace(pos, strlen("%USERNAME%"), username);
			return path;
		}

		// Scan the log file backwards and return the most recent line that
		static std::string GetLatestLineContaining(const std::string& term, const std::string& term2 = "")
		{
			std::ifstream f(ResolveLogPath(), std::ios::in | std::ios::binary);
			if (!f) return {};

			f.seekg(0, std::ios::end);
			std::streamoff size = f.tellg();
			if (size <= 0) return {};

			std::streamoff posSeek = size - 1;
			std::string buf;

			const auto Matches = [&](const std::string& line) {
				return line.find(term) != std::string::npos
					&& (term2.empty() || line.find(term2) != std::string::npos);
			};

			while (posSeek >= 0) {
				f.seekg(posSeek);
				char c = f.get();
				if (c == '\n' || c == '\r') {
					if (!buf.empty()) {
						std::reverse(buf.begin(), buf.end());
						if (Matches(buf))
							return buf;
						buf.clear();
					}
				}
				else {
					buf.push_back(c);
				}
				--posSeek;
			}

			if (!buf.empty()) {
				std::reverse(buf.begin(), buf.end());
				if (Matches(buf))
					return buf;
			}

			return {};
		}

		// Parse a TryHostGame log line.
		static SessionInfo ParseTryHostGameLine(const std::string& line)
		{
			SessionInfo info;

			auto extractField = [&](const std::string& key) -> std::string
			{
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

			auto extractBoolFlag = [&](const std::string& key) -> bool
			{
				const std::string search = "?" + key + "=";
				size_t pos = line.find(search);
				if (pos == std::string::npos) return false;
				pos += search.size();
				size_t end = line.find_first_of("?,)", pos);
				if (end == std::string::npos) end = line.size();
				return line.substr(pos, end - pos) == "true";
			};

			auto extractOptionField = [&](const std::string& key) -> std::string
			{
				const std::string search = "?" + key + "=";
				size_t pos = line.find(search);
				if (pos == std::string::npos) return {};
				pos += search.size();
				size_t end = line.find_first_of("?,)", pos);
				if (end == std::string::npos) end = line.size();
				return line.substr(pos, end - pos);
			};

			info.gameMode       = extractField("GameMode");
			info.gameDifficulty = extractField("GameDifficulty");
			info.mapName        = extractField("MapName");
			info.hardcore       = extractBoolFlag("HARDCORE");
			info.riftMode       = extractBoolFlag("RIFTMODE");
			info.bonusWave      = extractOptionField("BonusWave");
			info.valid          = !info.gameMode.empty() || !info.mapName.empty();

			return info;
		}

		// Extract the wave number from a "CombatPhase_..._XX has Ended" line.
		static int ParseCombatPhaseWave(const std::string& line)
		{
			const std::string startMarker = "CombatPhase_";
			const std::string endMarker   = " has Ended";

			const size_t startPos = line.find(startMarker);
			if (startPos == std::string::npos) return -1;

			const size_t endPos = line.find(endMarker, startPos);
			if (endPos == std::string::npos) return -1;

			const std::string phaseName = line.substr(startPos, endPos - startPos);

			const size_t lastUnderscore = phaseName.rfind('_');
			if (lastUnderscore == std::string::npos) return -1;

			const size_t numStart = lastUnderscore + 1;
			if (numStart >= phaseName.size()) return -1;

			try   { return std::stoi(phaseName.substr(numStart)); }
			catch (...) { return -1; }
		}

		static void ThreadFunc()
		{
			while (s_running.load()) {

				// abort detection
				if (LavenderHook::Globals::stop_on_fail) {
					try {
						auto found = GetLatestLineContaining("has been Aborted");
						if (!found.empty() && found != s_lastAbortLine) {
							s_lastAbortLine = found;
							s_hasNewAbort.store(true);
							s_lastAbortTime = std::chrono::steady_clock::now();
							try { LavenderConsole::GetInstance().Log("LogMonitor: stopped actions"); }
							catch (...) {}
						}
					}
					catch (...) {}
				}

				// session / map detection
				try {
					auto found = GetLatestLineContaining("LogOnline: TryHostGame");
					if (!found.empty() && found != s_lastTryHostGameLine) {
						s_lastTryHostGameLine = found;
						SessionInfo parsed = ParseTryHostGameLine(found);

						{
							std::lock_guard<std::mutex> lock(s_sessionMutex);
							s_currentSession = parsed;
						}

						if (parsed.valid) {
								try {
									auto& con = LavenderConsole::GetInstance();
									con.Log("[Session] Map: "        + parsed.mapName);
									con.Log("[Session] Mode: "       + parsed.gameMode
											+ "  |  Difficulty: "   + parsed.gameDifficulty);
									con.Log("[Session] Hardcore: "   + std::string(parsed.hardcore ? "Yes" : "No")
											+ "  |  Rift Mode: "    + std::string(parsed.riftMode  ? "Yes" : "No"));

									std::string effectiveBonusWave = parsed.bonusWave.empty()
										? XpTables::GetDefaultBonusWave(parsed.mapName)
										: parsed.bonusWave;
									con.Log("[Session] Bonus Wave: " + (effectiveBonusWave.empty() ? "None" : effectiveBonusWave));
								}
								catch (...) {}
							}
								}
							}
							catch (...) {}

							// wave end / XP detection
							try {
								auto found = GetLatestLineContaining("CombatPhase_", "has Ended");
								if (!found.empty() && found != s_lastCombatPhaseLine)
								{
									s_lastCombatPhaseLine = found;
									const int wave = ParseCombatPhaseWave(found);
									if (wave > 0) {
										SessionInfo session;
										{
											std::lock_guard<std::mutex> lock(s_sessionMutex);
											session = s_currentSession;
										}
										const std::string effectiveBw = session.bonusWave.empty()
											? XpTables::GetDefaultBonusWave(session.mapName)
											: session.bonusWave;
										const float xp = XpTables::CalculateWaveXp(
											session.mapName, effectiveBw,
											session.gameDifficulty, session.gameMode,
											session.hardcore, session.riftMode, wave);
										XpManager::AwardXp(xp);
									}
								}
							}
							catch (...) {}

							std::this_thread::sleep_for(250ms);
						}
					}

		void Start()
		{
			if (s_running.load()) return;
			XpManager::Init();
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
			// Return the current flag state to stop actions, but only reset it if it's been more than 1 second since the last detected abort line.
			if (!s_hasNewAbort.load()) return false;

			using namespace std::chrono;
			auto now = steady_clock::now();
			if (s_lastAbortTime.time_since_epoch().count() == 0)
				return true;

			if ((now - s_lastAbortTime) <= milliseconds(1000))
				return true;

			s_hasNewAbort.store(false);
			return false;
		}

		SessionInfo GetCurrentSession()
		{
			std::lock_guard<std::mutex> lock(s_sessionMutex);
			return s_currentSession;
		}

	}
}
