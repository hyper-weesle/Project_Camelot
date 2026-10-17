// Line-preserving editor for the port's Config.toml. Only `key = value` lines inside
// `[section]` tables are touched, so comments and settings the launcher does not know
// about survive a save untouched.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class ConfigFile {
public:
    bool Load(const std::filesystem::path& path);
    bool Save() const;

    const std::filesystem::path& Path() const { return path_; }

    // Raw TOML value text (strings keep their quotes), or nullopt when absent.
    std::optional<std::string> GetRaw(const std::string& section, const std::string& key) const;
    std::optional<std::string> GetString(const std::string& section, const std::string& key) const;
    std::optional<bool> GetBool(const std::string& section, const std::string& key) const;
    std::optional<double> GetNumber(const std::string& section, const std::string& key) const;

    void SetRaw(const std::string& section, const std::string& key, const std::string& rawValue);
    void SetString(const std::string& section, const std::string& key, const std::string& value);
    void SetBool(const std::string& section, const std::string& key, bool value);
    void SetNumber(const std::string& section, const std::string& key, double value);

private:
    struct Location {
        size_t sectionHeader = std::string::npos; // line index of `[section]`
        size_t sectionEnd = 0;                    // first line after the section
        size_t keyLine = std::string::npos;       // line index of `key = ...`
    };
    Location Find(const std::string& section, const std::string& key) const;

    std::filesystem::path path_;
    std::vector<std::string> lines_;
};
