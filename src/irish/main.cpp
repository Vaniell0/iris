/// @file src/irish/main.cpp
/// Entry point — three-mode detection, then dispatch.
#include "session/session.hpp"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "checker/checker.hpp"
#include "exec/executor.hpp"
#include <registry.hpp>
#include <unistd.h>
#include <cstdio>
#include <fstream>
#include <sstream>

static int run_script(const char* path, iris::irsh::Session& session);
static int run_repl(iris::irsh::Session& session);
static int run_pipeline_component(iris::irsh::Session& session);

int main(int argc, char** argv) {
    // Freeze global registry before any irsh statement runs.
    iris::TypeRegistry::global().freeze();

    iris::irsh::Session session;

    if (argc > 1) {
        return run_script(argv[1], session);
    }

    if (isatty(STDIN_FILENO)) {
        return run_repl(session);
    }

    // stdin is a pipe — pipeline-component mode
    return run_pipeline_component(session);
}

// ── TODO: implement ───────────────────────────────────────────────────────────

static int run_script(const char*, iris::irsh::Session&) {
    std::fprintf(stderr, "irish: script mode not yet implemented\n");
    return 2;
}

static int run_repl(iris::irsh::Session&) {
    std::fprintf(stderr, "irish: REPL not yet implemented\n");
    return 2;
}

static int run_pipeline_component(iris::irsh::Session&) {
    std::fprintf(stderr, "irish: pipeline-component mode not yet implemented\n");
    return 2;
}
