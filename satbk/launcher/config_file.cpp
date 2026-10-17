#include "config_file.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

std::string Trim(const std::string& text) {
    const size_t begin = text.find_first_not_of(" \t\r");
    if (begin == std::string::npos) {
        return {};
    }
    const size_t end = text.find_last_not_of(" \t\r");
    return text.substr(begin, end - begin + 1);
}

// The table name of a `[name]` header line, or empty for any other line.
std::string HeaderName(const std::string& line) {
    const std::string t = Trim(line);
    if (t.size() < 3 || t.front() != '[' || t[1] == '[') {
        return {};
    }
    const size_t close = t.find(']');
    return close == std::string::npos ? std::string() : Trim(t.substr(1, close - 1));
}

bool IsHeader(const std::string& line) {
    const std::string t = Trim(line);
    return !t.empty() && t.front() == '[';
}

// The key of a `key = value` line, or empty.
std::string KeyOf(const std::string& line) {
    const std::string t = Trim(line);
    if (t.empty() || t.front() == '#' || t.front() == '[') {
        return {};
    }
    const size_t eq = t.find('=');
    return eq == std::string::npos ? std::string() : Trim(t.substr(0, eq));
}

// Value text of a `key = value` line, without a trailing comment.
std::string ValueOf(const std::string& line) {
    const size_t eq = line.find('=');
    std::string value = Trim(line.substr(eq + 1));
    if (!value.empty() && value.front() == '"') {
        const size_t close = value.find('"', 1);
        return close == std::string::npos ? value : value.substr(0, close + 1);
    }
    const size_t hash = value.find('#');
    return Trim(hash == std::string::npos ? value : value.substr(0, hash));
}

std::string Quote(const std::string& value) {
    std::string out = "\"";
    for (char c : value) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out + "\"";
}

} // namespace

bool ConfigFile::Load(const std::filesystem::path& path) {
    path_ = path;
    lines_.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines_.push_back(line);
    }
    return true;
}

bool ConfigFile::Save() const {
    std::error_code ec;
    std::filesystem::create_directories(path_.parent_path(), ec);
    const std::filesystem::path temp = path_.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        for (const std::string& line : lines_) {
            out << line << '\n';
        }
        if (!out) {
            return false;
        }
    }
    std::filesystem::rename(temp, path_, ec);
    if (ec) {
        // Some file systems refuse to replace an existing file by rename.
        std::filesystem::copy_file(temp, path_, std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(temp, ec);
        return !ec;
    }
    return true;
}

ConfigFile::Location ConfigFile::Find(const std::string& section, const std::string& key) const {
    Location loc;
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (HeaderName(lines_[i]) != section) {
            continue;
        }
        loc.sectionHeader = i;
        size_t j = i + 1;
        for (; j < lines_.size() && !IsHeader(lines_[j]); ++j) {
            if (loc.keyLine == std::string::npos && KeyOf(lines_[j]) == key) {
                loc.keyLine = j;
            }
        }
        loc.sectionEnd = j;
        break;
    }
    return loc;
}

std::optional<std::string> ConfigFile::GetRaw(const std::string& section, const std::string& key) const {
    const Location loc = Find(section, key);
    if (loc.keyLine == std::string::npos) {
        return std::nullopt;
    }
    return ValueOf(lines_[loc.keyLine]);
}

std::optional<std::string> ConfigFile::GetString(const std::string& section, const std::string& key) const {
    auto raw = GetRaw(section, key);
    if (!raw) {
        return std::nullopt;
    }
    if (raw->size() >= 2 && raw->front() == '"' && raw->back() == '"') {
        std::string out;
        for (size_t i = 1; i + 1 < raw->size(); ++i) {
            if ((*raw)[i] == '\\' && i + 2 < raw->size()) {
                ++i;
            }
            out += (*raw)[i];
        }
        return out;
    }
    return raw;
}

std::optional<bool> ConfigFile::GetBool(const std::string& section, const std::string& key) const {
    const auto raw = GetRaw(section, key);
    if (!raw) {
        return std::nullopt;
    }
    if (*raw == "true") {
        return true;
    }
    if (*raw == "false") {
        return false;
    }
    return std::nullopt;
}

std::optional<double> ConfigFile::GetNumber(const std::string& section, const std::string& key) const {
    const auto raw = GetRaw(section, key);
    if (!raw || raw->empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const double value = std::strtod(raw->c_str(), &end);
    return end == raw->c_str() ? std::nullopt : std::optional<double>(value);
}

void ConfigFile::SetRaw(const std::string& section, const std::string& key, const std::string& rawValue) {
    const std::string line = key + " = " + rawValue;
    const Location loc = Find(section, key);
    if (loc.keyLine != std::string::npos) {
        lines_[loc.keyLine] = line;
        return;
    }
    if (loc.sectionHeader != std::string::npos) {
        // Append after the section's last setting, before any trailing blank lines.
        size_t insertAt = loc.sectionEnd;
        while (insertAt > loc.sectionHeader + 1 && Trim(lines_[insertAt - 1]).empty()) {
            --insertAt;
        }
        lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(insertAt), line);
        return;
    }
    if (!lines_.empty() && !Trim(lines_.back()).empty()) {
        lines_.emplace_back();
    }
    lines_.push_back("[" + section + "]");
    lines_.push_back(line);
}

void ConfigFile::SetString(const std::string& section, const std::string& key, const std::string& value) {
    SetRaw(section, key, Quote(value));
}

void ConfigFile::SetBool(const std::string& section, const std::string& key, bool value) {
    SetRaw(section, key, value ? "true" : "false");
}

void ConfigFile::SetNumber(const std::string& section, const std::string& key, double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.6g", value);
    std::string text = buffer;
    if (text.find_first_of(".eE") == std::string::npos) {
        text += ".0"; // keep floats as floats for the runtime's TOML reader
    }
    SetRaw(section, key, text);
}
