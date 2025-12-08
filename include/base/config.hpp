/**
 * @file config.hpp
 * @brief INI 格式配置文件解析器
 *
 * 提供线程安全的单例配置管理器，支持从 INI 文件加载配置，
 * 并按 section/key 方式访问配置项。
 */

#pragma once

#include <string>
#include <unordered_map>
#include <mutex>

namespace fix40 {

/**
 * @class Config
 * @brief 线程安全的 INI 配置文件解析器（单例模式）
 *
 * 支持标准 INI 格式：
 * - [section] 定义配置节
 * - key = value 定义配置项
 * - ; 或 # 开头的行为注释
 *
 * @note 该类为单例模式，通过 instance() 获取唯一实例
 *
 * @par 使用示例
 * @code
 * Config::instance().load("config.ini");
 * int port = Config::instance().get_int("server", "port", 9000);
 * @endcode
 */
class Config {
public:
    /**
     * @brief 获取 Config 单例实例
     * @return Config& 单例引用
     */
    static Config& instance();

    /**
     * @brief 从文件加载配置
     * @param filename 配置文件路径
     * @return true 加载成功
     * @return false 加载失败（文件不存在或无法打开）
     *
     * @note 调用此方法会清空之前加载的所有配置
     */
    bool load(const std::string& filename);

    /**
     * @brief 获取字符串类型的配置值
     * @param section 配置节名称
     * @param key 配置项名称
     * @param default_value 默认值，当配置项不存在时返回
     * @return std::string 配置值或默认值
     */
    std::string get(const std::string& section, const std::string& key, const std::string& default_value = "");

    /**
     * @brief 获取整数类型的配置值
     * @param section 配置节名称
     * @param key 配置项名称
     * @param default_value 默认值，当配置项不存在或转换失败时返回
     * @return int 配置值或默认值
     */
    int get_int(const std::string& section, const std::string& key, int default_value = 0);

    /**
     * @brief 获取浮点数类型的配置值
     * @param section 配置节名称
     * @param key 配置项名称
     * @param default_value 默认值，当配置项不存在或转换失败时返回
     * @return double 配置值或默认值
     */
    double get_double(const std::string& section, const std::string& key, double default_value = 0.0);

private:
    Config() = default;
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    /**
     * @brief 去除字符串首尾空白字符
     * @param str 输入字符串
     * @return std::string 去除空白后的字符串
     */
    std::string trim(const std::string& str);

    /// 配置数据存储：section -> (key -> value)
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> data_;
    /// 保护 data_ 的互斥锁
    std::mutex mutex_;
};

} // namespace fix40 