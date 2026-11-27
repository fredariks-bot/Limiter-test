#include "TestFramework.h"

int g_checks = 0;
int g_failures = 0;

void testDsp();
void testPlugin();

int main() {
    testDsp();
    testPlugin();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
