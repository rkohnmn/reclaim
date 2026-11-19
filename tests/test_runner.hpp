#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace reclaim::test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        registry().push_back(TestCase{name, std::move(fn)});
    }
};

#define RECLAIM_TEST(NAME)                                                   \
    static void test_fn_##NAME();                                            \
    static ::reclaim::test::Registrar reg_##NAME(#NAME, &test_fn_##NAME);    \
    static void test_fn_##NAME()

#define EXPECT_TRUE(EXPR)                                                    \
    do {                                                                     \
        if (!(EXPR)) {                                                       \
            std::ostringstream oss_;                                         \
            oss_ << __FILE__ << ":" << __LINE__ << " EXPECT_TRUE failed: "   \
                 << #EXPR;                                                   \
            throw std::runtime_error(oss_.str());                            \
        }                                                                    \
    } while (false)

#define EXPECT_FALSE(EXPR) EXPECT_TRUE(!(EXPR))

#define EXPECT_EQ(A, B)                                                      \
    do {                                                                     \
        const auto& a_ = (A);                                                \
        const auto& b_ = (B);                                                \
        if (!(a_ == b_)) {                                                   \
            std::ostringstream oss_;                                         \
            oss_ << __FILE__ << ":" << __LINE__ << " EXPECT_EQ failed: "     \
                 << #A << " (" << a_ << ") != " << #B << " (" << b_ << ")";  \
            throw std::runtime_error(oss_.str());                            \
        }                                                                    \
    } while (false)

inline int run_all_tests() {
    int passed = 0;
    int failed = 0;
    for (const auto& tc : registry()) {
        try {
            tc.fn();
            ++passed;
            std::cout << "[PASS] " << tc.name << "\n";
        } catch (const std::exception& ex) {
            ++failed;
            std::cerr << "[FAIL] " << tc.name << " -> " << ex.what() << "\n";
        }
    }
    std::cout << "\nSummary: " << passed << " passed, " << failed << " failed.\n";
    return failed == 0 ? 0 : 1;
}

}  // namespace reclaim::test
