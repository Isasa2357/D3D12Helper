//
// test_main.cpp
//
// 引数なし: 全 suite を実行。
// 引数あり: その suite だけ実行（CTest から機能単位で呼ぶために使う）。
//
#include "TestFramework.hpp"

int main(int argc, char** argv) {
    const char* suiteFilter = (argc > 1) ? argv[1] : nullptr;
    int failed = ::d3d12test::RunAll(suiteFilter);
    return failed == 0 ? 0 : 1;
}
