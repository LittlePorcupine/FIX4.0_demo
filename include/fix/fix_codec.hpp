#pragma once

#include <cstdint>
#include <ctime>
#include <iomanip>
#include <numeric>
#include <stdexcept>
#include <string>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <array>

#include "fix/fix_tags.hpp"

namespace fix40 {

// FIX 分隔符：ASCII 0x01
constexpr char SOH = '\x01';

/**
 * @class FixMessage
 * @brief 代表一个FIX报文的面向对象封装.
 *
 * 封装了报文的字段，提供了类型安全的访问接口，并隐藏了内部存储细节.
 */
class FixMessage {
public:
    // 设置字段值
    void set(int tag, const std::string& value) { fields_[tag] = value; }
    void set(int tag, int value) { fields_[tag] = std::to_string(value); }

    // 获取字段值
    std::string get_string(int tag) const {
        auto it = fields_.find(tag);
        if (it == fields_.end()) {
            throw std::runtime_error("Tag not found: " + std::to_string(tag));
        }
        return it->second;
    }

    int get_int(int tag) const {
        return std::stoi(get_string(tag));
    }

    bool has(int tag) const {
        return fields_.count(tag) > 0;
    }

    const std::unordered_map<int, std::string>& get_fields() const {
        return fields_;
    }

private:
    friend class FixCodec;
    std::unordered_map<int, std::string> fields_;
};


/**
 * @class FixCodec
 * @brief 负责FIX报文的序列化和反序列化.
 *
 * 将 FixMessage 对象编码为字符串，或将字符串解码为 FixMessage 对象.
 * 自动处理 BodyLength 和 CheckSum.
 */
class FixCodec {
public:
    // 将 FixMessage 对象编码为符合FIX协议的字符串
    std::string encode(FixMessage& msg) const {
        // 1. 准备时间戳
        char ts[32];
        generate_utc_timestamp(ts, sizeof(ts));
        msg.set(tags::SendingTime, ts);

        // 2. 构造标准 Header（除 8= 和 9= 之外）
        static constexpr std::array<int, 5> kStdHeaderOrder = {
            tags::MsgType,       // 35
            tags::SenderCompID,  // 49
            tags::TargetCompID,  // 56
            tags::MsgSeqNum,     // 34
            tags::SendingTime    // 52
        };

        std::ostringstream header_rest_ss;
        for (int tag : kStdHeaderOrder) {
            if (msg.has(tag)) {
                header_rest_ss << tag << "=" << msg.get_string(tag) << SOH;
            }
        }

        // 3. 构造报文体 (Body) —— 仅业务字段
        std::string body_str = build_body_from_message(msg);

        std::string header_rest = header_rest_ss.str();

        // 4. 计算 BodyLength （从 35= 起始到 CheckSum 前一个 SOH）
        std::size_t body_length_val = header_rest.size() + body_str.size();
        msg.set(tags::BodyLength, static_cast<int>(body_length_val));

        // 5. 构造最终前缀（8= & 9= + HeaderRest + Body）
        std::ostringstream prefix_ss;
        prefix_ss << tags::BeginString << "=" << "FIX.4.0" << SOH
                  << tags::BodyLength << "=" << msg.get_string(tags::BodyLength) << SOH
                  << header_rest
                  << body_str; // 如果 body_str 非空，则其已包含 SOH 分隔符

        std::string prefix = prefix_ss.str();

        // 6. 计算并附加校验和
        std::string checksum = calculate_checksum(prefix);
        return prefix + std::to_string(tags::CheckSum) + "=" + checksum + SOH;
    }

    // 将原始FIX字符串解码为 FixMessage 对象，同时进行校验
    FixMessage decode(const std::string& raw) const {
        // 1. 校验和验证
        const std::string checksum_tag = std::string(1, SOH) + std::to_string(tags::CheckSum) + "=";
        const size_t checksum_pos = raw.rfind(checksum_tag);
        if (checksum_pos == std::string::npos) {
            throw std::runtime_error("Tag 10 (Checksum) not found");
        }
        const std::string prefix = raw.substr(0, checksum_pos + 1);
        const std::string expected_checksum = raw.substr(checksum_pos + checksum_tag.length(), 3);
        const std::string actual_checksum = calculate_checksum(prefix);
        if (expected_checksum != actual_checksum) {
            throw std::runtime_error("Checksum mismatch: expected " + expected_checksum + ", got " + actual_checksum);
        }

        // 2. 逐字段解析
        FixMessage msg;
        size_t pos = 0;
        size_t next_soh;
        while ((next_soh = raw.find(SOH, pos)) != std::string::npos) {
            const std::string field = raw.substr(pos, next_soh - pos);
            if (field.empty()) {
                pos = next_soh + 1;
                continue;
            }
            const size_t eq_pos = field.find('=');
            if (eq_pos == std::string::npos) {
                throw std::runtime_error("Invalid field format: " + field);
            }
            int tag = std::stoi(field.substr(0, eq_pos));
            std::string value = field.substr(eq_pos + 1);
            msg.set(tag, value);
            pos = next_soh + 1;
        }

        // 3. BodyLength 验证
        const int body_len_from_msg = msg.get_int(tags::BodyLength);
        const size_t body_start_pos = raw.find(SOH, raw.find(std::to_string(tags::BodyLength) + "=")) + 1;
        const size_t actual_body_len = checksum_pos + 1 - body_start_pos;
        if (static_cast<size_t>(body_len_from_msg) != actual_body_len) {
            throw std::runtime_error("BodyLength mismatch: expected " + std::to_string(body_len_from_msg) + ", got " + std::to_string(actual_body_len));
        }

        return msg;
    }

private:
    std::string build_body_from_message(const FixMessage& msg) const {
        std::ostringstream body;

        // 定义哪些 tag 属于 Body，并按 tag 升序排列以提高一致性
        std::vector<int> body_tags;
        for (const auto& pair : msg.get_fields()) {
            // Body 部分包含除标准头 (8,9,35,49,56,34,52) 和尾 (10) 之外的所有字段
            if (pair.first != tags::BeginString && pair.first != tags::BodyLength && pair.first != tags::CheckSum &&
                pair.first != tags::MsgType && pair.first != tags::SenderCompID && pair.first != tags::TargetCompID &&
                pair.first != tags::MsgSeqNum && pair.first != tags::SendingTime) {
                body_tags.push_back(pair.first);
            }
        }
        std::sort(body_tags.begin(), body_tags.end());

        for (int tag : body_tags) {
            body << tag << "=" << msg.get_string(tag) << SOH;
        }

        return body.str();
    }

    std::string calculate_checksum(const std::string& data) const {
        const uint32_t sum = std::accumulate(data.begin(), data.end(), 0U);
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(3) << (sum % 256);
        return oss.str();
    }

    void generate_utc_timestamp(char* buf, size_t buf_size) const {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &t);
#else
        gmtime_r(&t, &tm);
#endif
        std::strftime(buf, buf_size, "%Y%m%d-%H:%M:%S", &tm);
    }
};
} // fix40 名称空间结束
