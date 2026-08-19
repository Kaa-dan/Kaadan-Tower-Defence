#include "TestFramework.h"

int main() {
    int passed = 0;
    for (const testing::TestCase& test : testing::registry()) {
        const int before = testing::failureCount();
        std::printf("  %s\n", test.name);
        test.fn();
        if (testing::failureCount() == before) {
            ++passed;
        }
    }

    const int total = static_cast<int>(testing::registry().size());
    std::printf("\n%d/%d tests passed, %d checks failed\n",
                passed, total, testing::failureCount());
    return testing::failureCount() == 0 ? 0 : 1;
}
