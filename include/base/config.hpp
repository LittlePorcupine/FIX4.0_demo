#pragma once

#include <string>
#include <unordered_map>
#include <mutex>

namespace fix40 {

class Config {
public:
    static Config& instance();

    bool load(const std::string& filename);

    std::string get(const std::string& section, const std::string& key, const std::string& default_value = "");
    int get_int(const std::string& section, const std::string& key, int default_value = 0);
    double get_double(const std::string& section, const std::string& key, double default_value = 0.0);

private:
    Config() = default;
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    std::string trim(const std::string& str);

    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> data_;
    std::mutex mutex_;
};

} // namespace fix40 