#include <cstdio>
#include <string_view>

namespace {

void print_usage() {
    std::puts("hlsl-clippy 0.0.0");
    std::puts("Usage: hlsl-clippy <command> [args]");
    std::puts("");
    std::puts("Commands:");
    std::puts("  lint <file>   Lint an HLSL source file (not yet implemented)");
    std::puts("  --help        Print this help");
    std::puts("  --version     Print version");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    const std::string_view cmd = argv[1];
    if (cmd == "--help" || cmd == "-h") {
        print_usage();
        return 0;
    }
    if (cmd == "--version" || cmd == "-v") {
        std::puts("0.0.0");
        return 0;
    }
    if (cmd == "lint") {
        std::puts("hlsl-clippy: lint not yet implemented");
        return 0;
    }
    print_usage();
    return 1;
}
