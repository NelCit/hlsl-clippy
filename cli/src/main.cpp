#include <iostream>
#include <string_view>

#include "hlsl_clippy/version.hpp"

namespace {

void print_usage() {
    std::cout << "hlsl-clippy " << hlsl_clippy::version() << "\n"
              << "Usage: hlsl-clippy <command> [args]\n"
              << "\n"
              << "Commands:\n"
              << "  lint <file>   Lint an HLSL source file (not yet implemented)\n"
              << "  --help        Print this help\n"
              << "  --version     Print version\n";
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
        std::cout << hlsl_clippy::version() << "\n";
        return 0;
    }
    if (cmd == "lint") {
        std::cout << "hlsl-clippy: lint not yet implemented\n";
        return 0;
    }
    print_usage();
    return 1;
}
