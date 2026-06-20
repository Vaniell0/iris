/// @file src/irish/backend/os_irsh.cpp
#include "os_irsh.hpp"
#include "../checker/checker.hpp"
#include <backend/os.hpp>
#include <registry.hpp>
#include <sdk/cpp/os.hpp>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace iris::irsh {

static std::string strip_quotes(std::string_view s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return std::string{s.substr(1, s.size() - 2)};
    return std::string{s};
}

// ── check ─────────────────────────────────────────────────────────────────────

IrType OsIrshBackend::check(std::string_view op,
                             const BackendConfig& /*config*/,
                             const IrType& /*input*/,
                             const TypeRegistry& global,
                             std::vector<TypeError>& errs,
                             Loc loc) const {
    auto lookup = [&](const char* type_name) -> IrType {
        if (auto* d = global.find(type_name)) return StreamType{d->id};
        errs.push_back({loc, std::string{"@os: "} + type_name + " not in registry"});
        return VoidType{};
    };
    if (op == "ls")                       return lookup("DirEntry");
    if (op == "ps")                       return lookup("ProcEntry");
    if (op == "env")                      return lookup("EnvEntry");
    if (op == "exec" || op == "run")      return TextLineType{};
    if (op == "clear")                    return VoidType{};
    errs.push_back({loc, "@os." + std::string{op} + ": unknown operation"});
    return VoidType{};
}

// ── make_gen ──────────────────────────────────────────────────────────────────

IrisGen OsIrshBackend::make_gen(std::string_view op,
                                 const BackendConfig& config,
                                 const TypeDescriptor* /*desc*/,
                                 IrisGen upstream) {
    std::string cfg;
    if (auto* s = std::get_if<std::string>(&config))
        cfg = strip_quotes(*s);

    if (op == "ls") {
        // Parse flags and optional path from config.
        // Config format: "-flags" or "path" or "-flags path"
        std::string flags, path;
        if (!cfg.empty() && cfg[0] == '-') {
            auto sp = cfg.find(' ');
            flags = cfg.substr(1, sp == std::string::npos ? std::string::npos : sp - 1);
            if (sp != std::string::npos) path = strip_quotes(cfg.substr(sp + 1));
        } else {
            path = cfg;
        }
        if (path.empty()) path = ".";

        bool show_hidden = flags.find('a') != std::string::npos;
        bool by_size     = flags.find('S') != std::string::npos;
        bool by_mtime    = flags.find('t') != std::string::npos;
        bool reverse     = flags.find('r') != std::string::npos;

        auto stream = std::make_shared<iris::os::LsStream>(path, show_hidden);
        IrisGen base_gen = [stream]() mutable -> IrisResult {
            iris::IrisValue v = stream->recv();
            if (v.type_id == 0) return iris_end();
            return iris_val(std::move(v));
        };

        if (by_size || by_mtime || reverse) {
            std::vector<iris::IrisValue> buf;
            while (auto _r = base_gen()) { if (!*_r) break; buf.push_back(std::move(**_r)); }
            if (by_size) {
                std::sort(buf.begin(), buf.end(), [](const auto& a, const auto& b) {
                    const auto* ea = reinterpret_cast<const DirEntry*>(a.raw().data());
                    const auto* eb = reinterpret_cast<const DirEntry*>(b.raw().data());
                    return ea->size > eb->size;
                });
            } else if (by_mtime) {
                std::sort(buf.begin(), buf.end(), [](const auto& a, const auto& b) {
                    const auto* ea = reinterpret_cast<const DirEntry*>(a.raw().data());
                    const auto* eb = reinterpret_cast<const DirEntry*>(b.raw().data());
                    return ea->mtime > eb->mtime;
                });
            }
            if (reverse) std::reverse(buf.begin(), buf.end());
            auto sbuf = std::make_shared<std::vector<iris::IrisValue>>(std::move(buf));
            return [sbuf, idx = size_t{0}]() mutable -> IrisResult {
                if (idx >= sbuf->size()) return iris_end();
                const auto& src = (*sbuf)[idx++];
                iris::IrisValue out;
                out.type_id = src.type_id;
                if (src.is_raw()) out.payload = src.raw();
                return iris_val(std::move(out));
            };
        }

        return base_gen;
    }
    if (op == "ps") {
        auto ps = std::make_shared<iris::os::PsStream>();
        return [ps]() mutable -> IrisResult {
            iris::IrisValue v = ps->recv();
            if (v.type_id == 0) return iris_end();
            return iris_val(std::move(v));
        };
    }
    if (op == "env") {
        auto env = std::make_shared<iris::os::EnvStream>();
        return [env]() mutable -> IrisResult {
            iris::IrisValue v = env->recv();
            if (v.type_id == 0) return iris_end();
            return iris_val(std::move(v));
        };
    }
    if (op == "exec" || op == "run") {
        // Build argv: prefer pre-expanded vector<string> from expand_exec,
        // fall back to space-splitting a raw string (legacy single-arg form).
        std::vector<std::string> args;
        if (auto* vs = std::get_if<std::vector<std::string>>(&config)) {
            args = *vs;
        } else if (!cfg.empty()) {
            std::istringstream iss{cfg};
            for (std::string tok; iss >> tok;) args.push_back(std::move(tok));
        }

        if (args.empty())
            return [up = std::move(upstream)]() mutable -> IrisResult {
                if (!up) return iris_end();
                return up();
            };

        int out_pfd[2];  // child stdout → parent
        if (::pipe(out_pfd) != 0)
            return []() -> IrisResult { return iris_end(); };

        // If upstream exists (stage position), wire it to child stdin
        int in_pfd[2] = {-1, -1};
        if (upstream) {
            if (::pipe(in_pfd) != 0) {
                ::close(out_pfd[0]); ::close(out_pfd[1]);
                return []() -> IrisResult { return iris_end(); };
            }
        }

        pid_t pid = ::fork();
        if (pid < 0) {
            ::close(out_pfd[0]); ::close(out_pfd[1]);
            if (in_pfd[0] != -1) { ::close(in_pfd[0]); ::close(in_pfd[1]); }
            return []() -> IrisResult { return iris_end(); };
        }
        if (pid == 0) {
            if (in_pfd[0] != -1) {
                ::dup2(in_pfd[0], STDIN_FILENO);
                ::close(in_pfd[0]); ::close(in_pfd[1]);
            }
            ::close(out_pfd[0]);
            ::dup2(out_pfd[1], STDOUT_FILENO);
            ::close(out_pfd[1]);
            ::setenv("CLICOLOR_FORCE", "1", 1);
            ::setenv("COLORTERM", "truecolor", 1);
            ::setenv("TERM", "xterm-256color", 0);
            std::vector<char*> argv;
            for (auto& a : args) argv.push_back(a.data());
            argv.push_back(nullptr);
            ::execvp(argv[0], argv.data());
            ::_exit(127);
        }

        ::close(out_pfd[1]);
        if (in_pfd[0] != -1) ::close(in_pfd[0]);

        // Feed upstream lines to child stdin in a background thread
        if (upstream && in_pfd[1] != -1) {
            auto write_fd = in_pfd[1];
            auto up_ptr   = std::make_shared<IrisGen>(std::move(upstream));
            auto fw_thread = std::thread([up_ptr, write_fd]() mutable {
                FILE* wf = ::fdopen(write_fd, "w");
                if (!wf) { ::close(write_fd); return; }
                while (auto _r = (*up_ptr)()) {
                    if (!*_r) break;
                    const auto& val = **_r;
                    if (val.is_str()) {
                        auto& s = std::get<std::string>(val.payload);
                        std::fwrite(s.data(), 1, s.size(), wf);
                        std::fputc('\n', wf);
                    }
                }
                std::fclose(wf);
            });
            fw_thread.detach();
        }

        auto f = std::shared_ptr<FILE>(::fdopen(out_pfd[0], "r"), ::fclose);
        auto done = std::make_shared<bool>(false);
        return [f, pid, done]() mutable -> IrisResult {
            if (*done) return iris_end();
            char buf[4096];
            if (!std::fgets(buf, sizeof(buf), f.get())) {
                ::waitpid(pid, nullptr, 0);
                *done = true;
                return iris_end();
            }
            std::string line{buf};
            if (!line.empty() && line.back() == '\n') line.pop_back();
            iris::IrisValue v; v.type_id = 0; v.payload = std::move(line);
            return iris_val(std::move(v));
        };
    }
    if (op == "clear") {
        std::fputs("\033[H\033[2J\033[3J", stdout);
        std::fflush(stdout);
        return []() -> IrisResult { return iris_end(); };
    }
    return []() -> IrisResult { return iris_end(); };
}

} // namespace iris::irsh
