/// @file src/irish/main.cpp
/// Entry point — mode detection, backend registration, then dispatch.
#include "session/session.hpp"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "parser/import_table.hpp"
#include "checker/checker.hpp"
#include "exec/executor.hpp"
#include "backend/os_irsh.hpp"
#include "backend/base_irsh.hpp"
#include "backend/ipc_irsh.hpp"
#include "backend/plugin_loader.hpp"
#include "repl/eval.hpp"
#include "repl/repl.hpp"
#include <registry.hpp>
#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>
#include <unistd.h>

static iris::irsh::BackendRegistry g_registry;

static int run_script(const char* path, iris::irsh::Session& session);
static int run_pipeline_component(iris::irsh::Session& session);

int main(int argc, char** argv) {
    // Session must be created before BaseIrshBackend so its registry reference is stable
    iris::irsh::Session session;

    // Register built-in backends (BaseIrshBackend holds TypeRegistry refs — must outlive registry)
    g_registry.register_backend(std::make_unique<iris::irsh::BaseIrshBackend>(
        iris::TypeRegistry::global(), session));
    g_registry.register_backend(std::make_unique<iris::irsh::OsIrshBackend>());
    g_registry.register_backend(std::make_unique<iris::irsh::IpcIrshBackend>());

    // Load plugins from ~/.iris/plugins/*.so (non-fatal: warn and continue)
    for (auto& err : iris::irsh::load_plugins(g_registry))
        std::fprintf(stderr, "irish: plugin warning: %s\n", err.c_str());

    g_registry.freeze();
    iris::TypeRegistry::global().freeze();

    // Load ~/.irshrc — default imports and user config (non-fatal)
    if (const char* home = std::getenv("HOME")) {
        std::string rc = std::string{home} + "/.irshrc";
        if (std::ifstream{rc}.good())
            run_script(rc.c_str(), session);
    }

    if (argc > 1 && std::string_view{argv[1]} == "--type-check" && argc > 2) {
        std::ifstream f{argv[2]};
        if (!f) { std::fprintf(stderr, "irish: cannot open '%s'\n", argv[2]); return 1; }
        iris::irsh::Checker checker{iris::TypeRegistry::global(), session.session_types(), g_registry};
        auto itbl = iris::irsh::make_import_table(g_registry, session);
        int rc = 0;
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            iris::irsh::Lexer lx{line};
            iris::irsh::Parser px{lx.tokenise(), itbl};
            auto pr = px.parse();
            if (!pr.ok()) {
                for (auto& e : pr.errors)
                    std::fprintf(stderr, "parse %u:%u: %s\n", e.loc.line, e.loc.col, e.msg.c_str());
                rc = 2; continue;
            }
            auto typed = checker.check(pr.program);
            if (!typed.ok()) {
                for (auto& e : typed.errors)
                    std::fprintf(stderr, "type %u:%u: %s\n", e.loc.line, e.loc.col, e.msg.c_str());
                rc = 2;
            }
        }
        if (!rc) std::puts("OK");
        return rc;
    }
    if (argc > 1 && std::string_view{argv[1]} == "-e" && argc > 2) {
        iris::irsh::Checker  checker{iris::TypeRegistry::global(), session.session_types(), g_registry};
        iris::irsh::Executor exec{session, g_registry};
        return iris::irsh::repl_eval(argv[2], session, checker, exec, g_registry);
    }
    if (argc > 1) {
        std::vector<std::string> sargs;
        for (int i = 2; i < argc; ++i) sargs.push_back(argv[i]);
        session.set_script_args(std::move(sargs));
        return run_script(argv[1], session);
    }
    if (isatty(STDIN_FILENO)) return iris::irsh::run_repl(session, g_registry);
    return run_pipeline_component(session);
}

static int run_script(const char* path, iris::irsh::Session& session) {
    std::ifstream f{path};
    if (!f) { std::fprintf(stderr, "irish: cannot open '%s'\n", path); return 1; }
    iris::irsh::Checker  checker{iris::TypeRegistry::global(), session.session_types(), g_registry};
    iris::irsh::Executor exec{session, g_registry, iris::irsh::ExecMode::Script};
    std::string continuation, line;
    int rc = 0;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line.back() == '\\') {
            continuation += line.substr(0, line.size() - 1);
            continuation += ' ';
            continue;
        }
        std::string input = continuation + line;
        continuation.clear();
        if (input.empty() || input[0] == '#') continue;
        int r = iris::irsh::repl_eval(input, session, checker, exec, g_registry);
        if (r) rc = r;
    }
    if (!continuation.empty()) {
        int r = iris::irsh::repl_eval(continuation, session, checker, exec, g_registry);
        if (r) rc = r;
    }
    return rc;
}

static int run_pipeline_component(iris::irsh::Session&) {
    std::fprintf(stderr, "irish: pipeline-component mode not yet implemented\n");
    return 2;
}
