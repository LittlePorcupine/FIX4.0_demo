/**
 * @file test_rapidcheck_integration.cpp
 * @brief RapidCheck 与 Catch2 集成验证测试
 * 
 * 此文件用于验证 RapidCheck 属性测试库是否正确集成到项目中。
 */

#include "../catch2/catch.hpp"
#include <rapidcheck.h>
#include <rapidcheck/catch.h>

TEST_CASE("RapidCheck 集成验证", "[rapidcheck][integration]") {
    
    rc::prop("加法交换律",
        [](int a, int b) {
            return (a + b) == (b + a);
        });
    
    rc::prop("乘法结合律",
        [](int a, int b, int c) {
            // 使用 long long 避免溢出
            long long la = a, lb = b, lc = c;
            return (la * lb) * lc == la * (lb * lc);
        });
    
    rc::prop("字符串连接长度",
        [](const std::string& s1, const std::string& s2) {
            return (s1 + s2).length() == s1.length() + s2.length();
        });
}

TEST_CASE("RapidCheck 生成器验证", "[rapidcheck][generators]") {
    
    rc::prop("生成的正整数确实为正",
        []() {
            auto n = *rc::gen::positive<int>();
            return n > 0;
        });
    
    rc::prop("生成的范围内整数在范围内",
        []() {
            auto n = *rc::gen::inRange(10, 100);
            return n >= 10 && n < 100;
        });
    
    rc::prop("生成的非空字符串确实非空",
        []() {
            auto s = *rc::gen::nonEmpty<std::string>();
            return !s.empty();
        });
}
