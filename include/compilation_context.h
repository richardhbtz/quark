#pragma once

class CLI;
class ErrorReporter;
class SourceManager;

struct CompilationContext {
    CLI& cli;
    ErrorReporter& errorReporter;
    SourceManager& sourceManager;
    
    CompilationContext(CLI& c, ErrorReporter& er, SourceManager& sm)
        : cli(c), errorReporter(er), sourceManager(sm) {}
};
