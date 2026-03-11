#pragma once

#include <string>
#include <filesystem>

std::string get_local_dll_version(const std::filesystem::path &dllPath);

// Returns -1 if a < b, 0 if a == b, 1 if a > b (dot-separated numeric version strings)
int compare_version_strings(const std::string &a, const std::string &b);
