#include <test_engi.hpp>

auto main() -> int
{
    auto app = engi::TestEngi::create();
    if (!app) return -1;

    app->run();

    return 0;
}