#pragma once

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

// A deliberately tiny test harness. The game core has no dependencies, and
// keeping the tests dependency-free too means they build and run on any
// platform the engine is ported to -- including a console devkit, where
// dropping in GoogleTest is not always an option.

namespace testing {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failureCount() {
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline void reportFailure(const char* file, int line, const std::string& message) {
    std::printf("    FAIL %s:%d\n         %s\n", file, line, message.c_str());
    ++failureCount();
}

} // namespace testing

#define TEST(name)                                                          \
    static void name();                                                     \
    static testing::Registrar registrar_##name(#name, name);                \
    static void name()

// Variadic so that braced initialisers -- CHECK(a == Cell{1, 2}) -- do not
// read as extra macro arguments.
#define CHECK(...)                                                          \
    do {                                                                    \
        if (!(__VA_ARGS__)) {                                               \
            testing::reportFailure(__FILE__, __LINE__,                      \
                                   "expected: " #__VA_ARGS__);              \
        }                                                                   \
    } while (false)

#define CHECK_EQ(actual, expected)                                          \
    do {                                                                    \
        const auto actualValue = (actual);                                  \
        const auto expectedValue = (expected);                              \
        if (!(actualValue == expectedValue)) {                              \
            testing::reportFailure(__FILE__, __LINE__,                      \
                std::string(#actual " == " #expected " -> got ") +          \
                std::to_string(actualValue) + ", expected " +               \
                std::to_string(expectedValue));                             \
        }                                                                   \
    } while (false)

#define CHECK_NEAR(actual, expected, tolerance)                             \
    do {                                                                    \
        const double diff = std::fabs(double(actual) - double(expected));   \
        if (diff > double(tolerance)) {                                     \
            testing::reportFailure(__FILE__, __LINE__,                      \
                std::string(#actual " ~= " #expected " -> off by ") +        \
                std::to_string(diff));                                      \
        }                                                                   \
    } while (false)
