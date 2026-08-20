#include "TestFramework.h"

#include <cstring>

namespace {

void printUsage() {
    std::printf("usage: ktd_tests [--list] [--filter <name>]\n"
                "  --list           print every registered test name, one per line\n"
                "  --filter <name>  run only the test with this exact name\n");
}

} // namespace

int main(int argc, char** argv) {
    const char* filter = nullptr;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--list") == 0) {
            for (const testing::TestCase& test : testing::registry()) {
                std::printf("%s\n", test.name);
            }
            return 0;
        }
        if (std::strcmp(argv[i], "--filter") == 0) {
            if (i + 1 >= argc) {
                std::printf("--filter needs a test name\n");
                return 2;
            }
            filter = argv[++i];
            continue;
        }
        printUsage();
        return 2;
    }

    int passed = 0;
    int ran = 0;
    for (const testing::TestCase& test : testing::registry()) {
        if (filter != nullptr && std::strcmp(test.name, filter) != 0) {
            continue;
        }
        ++ran;
        const int before = testing::failureCount();
        std::printf("  %s\n", test.name);
        test.fn();
        if (testing::failureCount() == before) {
            ++passed;
        }
    }

    // A filter that matches nothing is a failure, not a silent pass: it means a
    // test was renamed and CTest is still registering the old name.
    if (filter != nullptr && ran == 0) {
        std::printf("no test named '%s'\n", filter);
        return 2;
    }

    std::printf("\n%d/%d tests passed, %d checks failed\n",
                passed, ran, testing::failureCount());
    return testing::failureCount() == 0 ? 0 : 1;
}
