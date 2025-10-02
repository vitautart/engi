#include <app.hpp>

auto main() -> int
{
    auto app = engi::App::create();
    if (!app) return -1;

    app->run();

    return 0;
}