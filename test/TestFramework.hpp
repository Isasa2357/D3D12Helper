#pragma once
//
// TestFramework.hpp
//
// D3D12Helper 用の依存なし軽量テストハーネス。
// gtest / catch のような外部依存を持たず、自己登録するテストケースを
// 「機能（suite）」ごとに複数ファイルへ分割して書けるようにする。
//
//   TEST(suite, name) { ... CHECK(...); ... }
//
// のように書くと、静的初期化時にレジストリへ登録され、RunAll() でまとめて走る。
// CTest からは suite 名を引数に渡して機能単位で実行する（test/CMakeLists.txt 参照）。
//
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace d3d12test {

// 現在のケースを中断させるための例外。
struct TestFail { std::string message; };
struct TestSkip { std::string reason; };

struct TestCase {
    std::string suite;
    std::string name;
    std::function<void()> func;
};

// 静的初期化順序問題を避けるため、関数ローカル static でレジストリを保持する。
inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> registry;
    return registry;
}

struct Registrar {
    Registrar(const char* suite, const char* name, std::function<void()> fn) {
        Registry().push_back(TestCase{ suite, name, std::move(fn) });
    }
};

// ---- アサーション本体 ----
inline void DoCheck(bool cond, const char* expr, const char* file, int line) {
    if (!cond) {
        std::ostringstream os;
        os << file << ":" << line << ": CHECK failed: " << expr;
        throw TestFail{ os.str() };
    }
}

template <class A, class B>
void DoCheckEq(const A& a, const B& b, const char* ea, const char* eb,
               const char* file, int line) {
    if (!(a == b)) {
        std::ostringstream os;
        os << file << ":" << line << ": CHECK_EQ failed: " << ea << " == " << eb
           << "  (" << a << " vs " << b << ")";
        throw TestFail{ os.str() };
    }
}

inline void DoCheckNear(double a, double b, double eps, const char* ea, const char* eb,
                        const char* file, int line) {
    double d = a - b;
    if (d < 0) d = -d;
    if (d > eps) {
        std::ostringstream os;
        os << file << ":" << line << ": CHECK_NEAR failed: " << ea << " ~ " << eb
           << "  (|" << a << " - " << b << "| = " << d << " > " << eps << ")";
        throw TestFail{ os.str() };
    }
}

inline int RunAll(const char* suiteFilter) {
    int passed = 0, failed = 0, skipped = 0;
    for (const auto& tc : Registry()) {
        if (suiteFilter && tc.suite != suiteFilter) continue;
        std::cout << "[ RUN      ] " << tc.suite << "." << tc.name << "\n";
        try {
            tc.func();
            std::cout << "[       OK ] " << tc.suite << "." << tc.name << "\n";
            ++passed;
        } catch (const TestSkip& s) {
            std::cout << "[     SKIP ] " << tc.suite << "." << tc.name
                      << "  : " << s.reason << "\n";
            ++skipped;
        } catch (const TestFail& f) {
            std::cout << "[     FAIL ] " << tc.suite << "." << tc.name << "\n"
                      << "    " << f.message << "\n";
            ++failed;
        } catch (const std::exception& e) {
            std::cout << "[     FAIL ] " << tc.suite << "." << tc.name << "\n"
                      << "    unexpected exception: " << e.what() << "\n";
            ++failed;
        } catch (...) {
            std::cout << "[     FAIL ] " << tc.suite << "." << tc.name << "\n"
                      << "    unknown exception\n";
            ++failed;
        }
    }
    std::cout << "\n==== " << passed << " passed, " << failed << " failed, "
              << skipped << " skipped ====\n";
    return failed;
}

} // namespace d3d12test

// ---- マクロ ----
#define D3D12TEST_CONCAT_(a, b) a##b
#define D3D12TEST_CONCAT(a, b)  D3D12TEST_CONCAT_(a, b)

#define TEST(suite, name)                                                        \
    static void D3D12TEST_CONCAT(test_##suite##_##name##_, __LINE__)();           \
    static ::d3d12test::Registrar                                                 \
        D3D12TEST_CONCAT(reg_##suite##_##name##_, __LINE__)(                       \
            #suite, #name,                                                        \
            &D3D12TEST_CONCAT(test_##suite##_##name##_, __LINE__));                \
    static void D3D12TEST_CONCAT(test_##suite##_##name##_, __LINE__)()

// static_cast<bool> allows wrappers with explicit operator bool() to be tested
// without weakening their production API to an implicit conversion.
#define CHECK(cond)          ::d3d12test::DoCheck(static_cast<bool>(cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b)       ::d3d12test::DoCheckEq((a), (b), #a, #b, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, e)  ::d3d12test::DoCheckNear((a), (b), (e), #a, #b, __FILE__, __LINE__)
#define CHECK_THROWS(expr)                                                        \
    do {                                                                          \
        bool threw_ = false;                                                      \
        try { (expr); } catch (...) { threw_ = true; }                            \
        ::d3d12test::DoCheck(threw_, "expected exception: " #expr,                \
                             __FILE__, __LINE__);                                 \
    } while (0)
#define CHECK_NOTHROW(expr)                                                       \
    do {                                                                          \
        try { (expr); }                                                           \
        catch (const std::exception& e_) {                                        \
            ::d3d12test::DoCheck(false,                                           \
                ("unexpected exception: " #expr), __FILE__, __LINE__);            \
            (void)e_;                                                             \
        } catch (...) {                                                           \
            ::d3d12test::DoCheck(false,                                           \
                ("unexpected exception: " #expr), __FILE__, __LINE__);            \
        }                                                                         \
    } while (0)
#define TEST_SKIP(reason) throw ::d3d12test::TestSkip{ (reason) }
#define TEST_FAIL(msg)    throw ::d3d12test::TestFail{ (msg) }
