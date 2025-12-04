#include "../catch2/catch.hpp"
#include "base/config.hpp"
#include <fstream>
#include <cstdio>

using namespace fix40;

// 辅助函数：创建临时配置文件
class TempConfigFile {
public:
    explicit TempConfigFile(const std::string& content) 
        : filename_("/tmp/test_config_" + std::to_string(rand()) + ".ini") {
        std::ofstream file(filename_);
        file << content;
        file.close();
    }
    
    ~TempConfigFile() {
        std::remove(filename_.c_str());
    }
    
    const std::string& path() const { return filename_; }
    
private:
    std::string filename_;
};

TEST_CASE("Config load valid file", "[config]") {
    TempConfigFile file(
        "[server]\n"
        "port = 9000\n"
        "name = test_server\n"
        "\n"
        "[client]\n"
        "timeout = 30\n"
    );
    
    // 注意：Config 是单例，测试之间可能有状态残留
    // 每次 load 会清空之前的数据
    REQUIRE(Config::instance().load(file.path()));
    
    REQUIRE(Config::instance().get("server", "port") == "9000");
    REQUIRE(Config::instance().get("server", "name") == "test_server");
    REQUIRE(Config::instance().get("client", "timeout") == "30");
}

TEST_CASE("Config get with default value", "[config]") {
    TempConfigFile file("[section]\nkey = value\n");
    Config::instance().load(file.path());
    
    // 存在的 key
    REQUIRE(Config::instance().get("section", "key", "default") == "value");
    
    // 不存在的 key
    REQUIRE(Config::instance().get("section", "nonexistent", "default") == "default");
    
    // 不存在的 section
    REQUIRE(Config::instance().get("nosection", "key", "default") == "default");
}

TEST_CASE("Config get_int", "[config]") {
    TempConfigFile file(
        "[numbers]\n"
        "positive = 42\n"
        "negative = -10\n"
        "zero = 0\n"
        "invalid = abc\n"
    );
    Config::instance().load(file.path());
    
    REQUIRE(Config::instance().get_int("numbers", "positive", 0) == 42);
    REQUIRE(Config::instance().get_int("numbers", "negative", 0) == -10);
    REQUIRE(Config::instance().get_int("numbers", "zero", 99) == 0);
    
    // 无效值返回默认值
    REQUIRE(Config::instance().get_int("numbers", "invalid", 99) == 99);
    
    // 不存在的 key 返回默认值
    REQUIRE(Config::instance().get_int("numbers", "missing", 123) == 123);
}

TEST_CASE("Config get_double", "[config]") {
    TempConfigFile file(
        "[floats]\n"
        "pi = 3.14159\n"
        "negative = -2.5\n"
        "integer = 42\n"
        "invalid = not_a_number\n"
    );
    Config::instance().load(file.path());
    
    REQUIRE(Config::instance().get_double("floats", "pi", 0.0) == Approx(3.14159));
    REQUIRE(Config::instance().get_double("floats", "negative", 0.0) == Approx(-2.5));
    REQUIRE(Config::instance().get_double("floats", "integer", 0.0) == Approx(42.0));
    
    // 无效值返回默认值
    REQUIRE(Config::instance().get_double("floats", "invalid", 1.5) == Approx(1.5));
}

TEST_CASE("Config handles comments", "[config]") {
    TempConfigFile file(
        "; This is a comment\n"
        "# This is also a comment\n"
        "[section]\n"
        "key = value ; inline comment should NOT be stripped\n"
    );
    Config::instance().load(file.path());
    
    // 注意：当前实现不处理行内注释，整个值都会被读取
    REQUIRE(Config::instance().get("section", "key").find("value") != std::string::npos);
}

TEST_CASE("Config handles whitespace", "[config]") {
    TempConfigFile file(
        "[section]\n"
        "  key1  =  value1  \n"
        "key2=value2\n"
        "  key3   =   value3   \n"
    );
    Config::instance().load(file.path());
    
    REQUIRE(Config::instance().get("section", "key1") == "value1");
    REQUIRE(Config::instance().get("section", "key2") == "value2");
    REQUIRE(Config::instance().get("section", "key3") == "value3");
}

TEST_CASE("Config handles empty file", "[config]") {
    TempConfigFile file("");
    REQUIRE(Config::instance().load(file.path()));
    
    // 空文件，所有查询返回默认值
    REQUIRE(Config::instance().get("any", "key", "default") == "default");
}

TEST_CASE("Config load nonexistent file", "[config]") {
    REQUIRE_FALSE(Config::instance().load("/nonexistent/path/config.ini"));
}

TEST_CASE("Config handles section without keys", "[config]") {
    TempConfigFile file(
        "[empty_section]\n"
        "[section_with_data]\n"
        "key = value\n"
    );
    Config::instance().load(file.path());
    
    REQUIRE(Config::instance().get("empty_section", "key", "default") == "default");
    REQUIRE(Config::instance().get("section_with_data", "key") == "value");
}
