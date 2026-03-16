#include "LogMonitor.h"
#include "../xpsystem/XpTables.h"
#include "../xpsystem/XpManager.h"
#include "../Globals.h"
#include "../../ui/components/Console.h"

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

		// wave tracker
		static std::atomic<int> s_currentWave{ 0 };
		static std::string      s_lastBuildPhaseLine;

		// tavern tracking
		static std::atomic<bool> s_inTavern{ false };
		static std::string       s_lastTavernLine;

		// console map log deduplication
		static std::string       s_lastLoggedMapName;

		// wave detection from non-Tavern map load lines
		static std::string       s_lastNonTavernMapLine;

		// boss wave tracking
		static std::atomic<bool> s_isBossWave{ false };
		static std::string       s_lastBossPhaseLine;

		// skip the first BuildPhase update after a session
		static std::atomic<bool> s_skipNextBuildPhase{ false };

		// combat phase tracking — wave-number pair matching
		static std::atomic<bool> s_isCombatPhase{ false };
		static std::string       s_lastCombatPhaseStartedLine;
		static std::atomic<int>  s_lastSeenCombatStartWave{ 0 };
		static std::atomic<int>  s_lastSeenCombatEndWave{ 0 };

		// combat abort tracking
		static std::atomic<bool> s_combatAborted{ false };
		static std::string       s_lastCombatAbortLine;

		// pre summary phase tracking
		static std::atomic<bool> s_isPreSummaryPhase{ false };
		static std::string       s_lastPreSummaryPhaseLine;

		// Resolve the DDS log path once
		static std::string ResolveLogPath()
		{
			char localAppData[MAX_PATH]; size_t outlen = 0;
			getenv_s(&outlen, localAppData, sizeof(localAppData), "LOCALAPPDATA");
			if (outlen == 0) return {};
			return std::string(localAppData) + "\\DDS\\Saved\\Logs\\DDS.log";
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

			const std::string waveStr = extractOptionField("Wave");
			if (!waveStr.empty())
				try { info.startWave = std::stoi(waveStr); } catch (...) {}

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

		// Extract the wave number from a "BuildPhase_XX has Started" line.
		static int ParseBuildPhaseStarted(const std::string& line)
		{
			const std::string startMarker = "BuildPhase_";
			const std::string endMarker   = " has Started";

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

		// Extract the wave number from a "CombatPhase_..._XX has Started" line.
		static int ParseCombatPhaseStartedWave(const std::string& line)
		{
			const std::string startMarker = "CombatPhase_";
			const std::string endMarker   = " has Started";

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

						// combat abort detection
						try {
							auto found = GetLatestLineContaining("has been Aborted");
							if (!found.empty() && found != s_lastCombatAbortLine) {
								s_lastCombatAbortLine = found;
								s_combatAborted.store(true);
								s_isCombatPhase.store(false);
							}
						}
						catch (...) {}

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

						// Reset wave tracking and tavern state for the new session
						s_inTavern.store(false);
						if (parsed.startWave > 0)
							s_currentWave.store(parsed.startWave);
						else if (parsed.valid)
							s_currentWave.store(1);
						s_skipNextBuildPhase.store(parsed.startWave > 0);
						s_lastBuildPhaseLine = GetLatestLineContaining("BuildPhase_", "has Started");
						s_lastTavernLine     = GetLatestLineContaining("/Game/DDS/Maps/", "Tavern");
						s_isBossWave.store(false);
							Globals::boss_phase_active.store(false);
							s_lastBossPhaseLine  = GetLatestLineContaining("BossCombatPhase_", "has Started");
							s_isCombatPhase.store(false);
							s_combatAborted.store(false);
							s_lastCombatPhaseStartedLine = GetLatestLineContaining("CombatPhase_", "has Started");
							s_lastSeenCombatStartWave.store(0);
							s_lastSeenCombatEndWave.store(0);
							s_isPreSummaryPhase.store(false);
							s_lastPreSummaryPhaseLine = GetLatestLineContaining("PreSummaryPhase_", "has Started");

							if (parsed.valid && parsed.mapName != s_lastLoggedMapName) {
							s_lastLoggedMapName = parsed.mapName;
							try {
								auto& con = LavenderConsole::GetInstance();
								con.Log("[Session] Map: "        + parsed.mapName);
								con.Log("[Session] Mode: "       + parsed.gameMode);
								con.Log("[Session] Difficulty: " + parsed.gameDifficulty);
								con.Log("[Session] Hardcore: "   + std::string(parsed.hardcore ? "Yes" : "No"));
								con.Log("[Session] Rift Mode: "  + std::string(parsed.riftMode  ? "Yes" : "No"));

								std::string effectiveBonusWave = parsed.bonusWave.empty()
									? XpTables::GetDefaultBonusWave(parsed.mapName)
									: parsed.bonusWave;
								con.Log("[Session] Bonus Wave: " + (effectiveBonusWave.empty() ? "None" : effectiveBonusWave));

								const std::string waveLabel = (parsed.startWave > 0)
									? std::to_string(parsed.startWave)
									: "1";
								con.Log("[Session] Wave: " + waveLabel);
							}
							catch (...) {}
						}
								}
								}
								catch (...) {}

								// Tavern map detection
								try {
									auto found = GetLatestLineContaining("/Game/DDS/Maps/", "Tavern");
									if (!found.empty() && found != s_lastTavernLine) {
										s_lastTavernLine = found;
											s_inTavern.store(true);
											s_currentWave.store(0);
											s_isBossWave.store(false);
											Globals::boss_phase_active.store(false);
											Globals::enemies_alive.store(0);
											Globals::enemies_max.store(0);
											Globals::boss_health_current.store(0.f);
											Globals::boss_health_max.store(0.f);
											Globals::wave_time.store(0);
										s_lastBuildPhaseLine = GetLatestLineContaining("BuildPhase_", "has Started");
												s_lastBossPhaseLine  = GetLatestLineContaining("BossCombatPhase_", "has Started");
												s_isCombatPhase.store(false);
												s_combatAborted.store(false);
												s_lastCombatPhaseStartedLine = GetLatestLineContaining("CombatPhase_", "has Started");
												s_lastSeenCombatStartWave.store(0);
												s_lastSeenCombatEndWave.store(0);
												s_isPreSummaryPhase.store(false);
												s_lastPreSummaryPhaseLine = GetLatestLineContaining("PreSummaryPhase_", "has Started");
												{
											std::lock_guard<std::mutex> lock(s_sessionMutex);
											s_currentSession = SessionInfo{};
											s_currentSession.mapName = "Tavern";
										}
										if (s_lastLoggedMapName != "Tavern") {
											s_lastLoggedMapName = "Tavern";
											try { LavenderConsole::GetInstance().Log("[Session] Map: Tavern"); }
											catch (...) {}
										}
									}
									}
									catch (...) {}

									// Wave detection
									try {
										auto found = GetLatestLineContaining("/Game/DDS/Maps/", "?Wave=");
										if (!found.empty() && found != s_lastNonTavernMapLine
											&& found.find("Tavern") == std::string::npos)
										{
											s_lastNonTavernMapLine = found;
											const std::string key = "?Wave=";
											const size_t valStart = found.find(key) + key.size();
											size_t valEnd = found.find_first_of("?)", valStart);
											if (valEnd == std::string::npos) valEnd = found.size();
											try {
												const int w = std::stoi(found.substr(valStart, valEnd - valStart));
												if (w > 0) s_currentWave.store(w);
											}
											catch (...) {}
										}
									}
									catch (...) {}

									// wave end / XP detection
									try {
										auto found = GetLatestLineContaining("CombatPhase_", "has Ended");
										if (!found.empty()) {
											const int endWave = ParseCombatPhaseWave(found);
											if (endWave > 0 && endWave > s_lastSeenCombatEndWave.load()) {
												s_lastSeenCombatEndWave.store(endWave);
												s_lastCombatPhaseLine = found;
												if (endWave >= s_lastSeenCombatStartWave.load())
													s_isCombatPhase.store(false);
												if (!s_inTavern.load()) {
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
														session.hardcore, session.riftMode, endWave);
													XpManager::AwardXp(xp);
												}
											}
										}
									}
									catch (...) {}

												// boss wave detection
												try {
													auto found = GetLatestLineContaining("BossCombatPhase_", "has Started");
													if (!found.empty() && found != s_lastBossPhaseLine) {
														s_lastBossPhaseLine = found;
														if (!s_inTavern.load()) {
															s_isBossWave.store(true);
															Globals::boss_phase_active.store(true);
														}
													}
												}
																	catch (...) {}

																	// combat phase start detection
																	try {
																		auto found = GetLatestLineContaining("CombatPhase_", "has Started");
																		if (!found.empty()) {
																			const int startWave = ParseCombatPhaseStartedWave(found);
																			if (startWave > 0 && startWave > s_lastSeenCombatStartWave.load()) {
																				s_lastSeenCombatStartWave.store(startWave);
																				s_lastCombatPhaseStartedLine = found;
																				if (!s_inTavern.load()) {
																					s_isCombatPhase.store(true);
																					s_combatAborted.store(false);
																				}
																			}
																		}
																	}
																	catch (...) {}

																	// presummary phase detection
																	try {
																		auto found = GetLatestLineContaining("PreSummaryPhase_", "has Started");
																		if (!found.empty() && found != s_lastPreSummaryPhaseLine) {
																			s_lastPreSummaryPhaseLine = found;
																			if (!s_inTavern.load()) {
																				s_isPreSummaryPhase.store(true);
																			}
																		}
																	}
																	catch (...) {}

																					// build phase / wave tracking
																	try {
																		auto found = GetLatestLineContaining("BuildPhase_", "has Started");
																		if (!found.empty() && found != s_lastBuildPhaseLine)
																		{
																			s_lastBuildPhaseLine = found;
																			if (!s_inTavern.load()) {
																				const int buildWave = ParseBuildPhaseStarted(found);
																				if (buildWave > 0) {
																					s_isBossWave.store(false);
																					Globals::boss_phase_active.store(false);
																					if (s_skipNextBuildPhase.load()) {
																						s_skipNextBuildPhase.store(false);
																					} else {
																						s_currentWave.store(buildWave + 1);
																					}
																				}
																			}
																		}
																	}
																	catch (...) {}

									std::this_thread::sleep_for(20ms);
						}
					}

		void Start()
		{
			if (s_running.load()) return;
			XpManager::Init();
			s_lastCombatAbortLine = GetLatestLineContaining("has been Aborted");
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

		int GetCurrentWave()
		{
			return s_currentWave.load();
		}

		bool IsInTavern()
		{
			return s_inTavern.load();
		}

		bool IsBossWave()
		{
			return s_isBossWave.load();
		}

		bool IsCombatPhase()
		{
			return s_isCombatPhase.load();
		}

		bool IsCombatAborted()
		{
			return s_combatAborted.load();
		}

		bool IsPreSummaryPhase()
		{
			return s_isPreSummaryPhase.load();
		}

		int GetMaxWave()
		{
			SessionInfo session;
			{ std::lock_guard<std::mutex> lock(s_sessionMutex); session = s_currentSession; }
			const std::string& gm = session.gameMode;
			if (gm == "Gamemode.PureStrategy" ||
				gm == "Gamemode.Survival"      ||
				gm == "Gamemode.Survival.Mixed")
				return 25;
			if (gm == "Gamemode.Campaign")
				return 5;
			return 0;
		}

	}
}
