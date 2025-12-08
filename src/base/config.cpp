/**
 * @file config.cpp
 * @brief Config 类实现
 */

#include "base/config.hpp"
#include <fstream>
#include <algorithm>
#include <iostream>

namespace fix40 {

Config& Config::instance() {
    static Config inst;
    return inst;
}

std::string Config::trim(const std::string& str) {
    const char* whitespace = " \t\n\r\f\v";
    size_t first = str.find_first_not_of(whitespace);
    if (std::string::npos == first) {
        return "";
    }
    size_t last = str.find_last_not_of(whitespace);
    return str.substr(first, (last - first + 1));
}

bool Config::load(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    data_.clear();

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open config file " << filename << std::endl;
        return false;
    }

    std::string line;
    std::string current_section;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        if (line[0] == '[' && line.back() == ']') {
            current_section = trim(line.substr(1, line.length() - 2));
        } else {
            size_t delimiter_pos = line.find('=');
            if (delimiter_pos != std::string::npos) {
                std::string key = trim(line.substr(0, delimiter_pos));
                std::string value = trim(line.substr(delimiter_pos + 1));
                if (!key.empty()) {
                    data_[current_section][key] = value;
                }
            }
        }
    }
    return true;
}

std::string Config::get(const std::string& section, const std::string& key, const std::string& default_value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto sec_it = data_.find(section);
    if (sec_it != data_.end()) {
        auto key_it = sec_it->second.find(key);
        if (key_it != sec_it->second.end()) {
            return key_it->second;
        }
    }
    return default_value;
}

int Config::get_int(const std::string& section, const std::string& key, int default_value) {
    std::string val_str = get(section, key, "");
    if (val_str.empty()) {
        return default_value;
    }
    try {
        return std::stoi(val_str);
    } catch (const std::exception& e) {
        return default_value;
    }
}

double Config::get_double(const std::string& section, const std::string& key, double default_value) {
    std::string val_str = get(section, key, "");
    if (val_str.empty()) {
        return default_value;
    }
    try {
        return std::stod(val_str);
    } catch (const std::exception& e) {
        return default_value;
    }
}

} // namespace fix40 