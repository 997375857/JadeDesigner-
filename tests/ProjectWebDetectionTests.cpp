#include "../src/ProjectWebDetection.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {
int passed = 0;
void Check(bool value, const char* name)
{
    if (!value) { std::fprintf(stderr, "FAIL %s\n", name); std::exit(1); }
    ++passed;
    std::printf("PASS %s\n", name);
}
}

int wmain(int argc, wchar_t** argv)
{
    using ProjectWebDetection::Marker;
    Check(Marker("<button data-jade-handler=\"导入账号_被单击\">导入账号</button>"
        "<script src=\"app.js?v=20260904-12\"></script>") == "data-jade-handler",
        "handler-only HTML with an external script enables Jade preview");
    for (const std::string marker : {"data-jade-control", "data-jade-channel", "jade.invoke", "window.jade"})
        Check(Marker(marker) == marker, "existing Jade markers still enable preview");
    Check(Marker("").empty(), "empty HTML does not enable takeover");
    Check(Marker("<button id=\"save\">Save</button><script src=\"app.js\"></script>").empty(),
        "ordinary HTML without Jade markers does not enable takeover");
    if (argc > 1) {
        std::ifstream input(std::filesystem::path(argv[1]), std::ios::binary);
        Check(input.is_open(), "project HTML can be read");
        const std::string html{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        Check(Marker(html) == "data-jade-handler", "actual card-forward project enables preview via handler metadata");
    }
    std::printf("%d project web detection checks passed\n", passed);
}
