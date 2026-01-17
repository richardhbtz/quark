
#include "lsp/lsp_server.h"
#include <iostream>
#include <cstring>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

void printUsage(const char* programName) {
    std::cerr << "Quark Language Server\n\n";
    std::cerr << "Usage: " << programName << " [options]\n\n";
    std::cerr << "Options:\n";
    std::cerr << "  --stdio      Use stdio for communication (default)\n";
    std::cerr << "  --version    Print version information\n";
    std::cerr << "  --help       Print this help message\n";
}

void printVersion() {
    std::cerr << "quark-lsp version 1.0.0\n";
}

int main(int argc, char* argv[]) {
    bool useStdio = true;
    
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        }
        else if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0) {
            printVersion();
            return 0;
        }
        else if (std::strcmp(argv[i], "--stdio") == 0) {
            useStdio = true;
        }
        else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }
    
    if (!useStdio) {
        std::cerr << "Only stdio transport is currently supported.\n";
        return 1;
    }
    
    
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    
    lsp::LspServer server;
    server.run();
    return 0;
}
