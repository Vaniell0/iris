/// @file   include/backend/os.hpp
/// @brief  OsBackend — lazy OS command source using CRTP stream pattern.
///
/// OsBackend satisfies the Backend concept and streams typed OS data
/// (DirEntry, ProcEntry, EnvEntry) through recv(). emit() is a no-op.
/// Each recv() call reads exactly one entry from the OS — no upfront buffering.

#pragma once

#include <backend.hpp>
#include <sdk/cpp/os.hpp>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include <dirent.h>

namespace iris::os {

/// Failure reason returned by the eager convenience functions.
enum class OsError {
    NotFound,         ///< path or resource does not exist
    PermissionDenied, ///< insufficient privileges
    ReadError,        ///< underlying read failed
};

/// Walk @p path and return one DirEntry per visible entry (eager).
std::expected<std::vector<IrisValue>, OsError> ls(std::string_view path = ".");
/// Read /proc and return one ProcEntry per running process (eager).
std::expected<std::vector<IrisValue>, OsError> ps();
/// Return one EnvEntry per process environment variable (eager).
std::expected<std::vector<IrisValue>, OsError> env();

// ── OsStream CRTP base ────────────────────────────────────────────────────────
//
// Canonical pattern for lazy OS iterators.
// Derived must implement: bool open(), optional<Entry> next(), void close().
// ~OsStream calls close() if the stream was successfully opened.

template<typename Derived, typename Entry>
class OsStream {
protected:
    bool opened_ = false;
    bool done_   = false;
public:
    OsStream() = default;
    OsStream(const OsStream&) = delete;
    OsStream& operator=(const OsStream&) = delete;

    OsStream(OsStream&& o) noexcept : opened_(o.opened_), done_(o.done_) {
        o.opened_ = false;  // source no longer owns the open resource
        o.done_   = true;
    }
    OsStream& operator=(OsStream&&) = delete;

    ~OsStream() {
        if (opened_) static_cast<Derived*>(this)->close();
    }

    IrisValue recv() {
        if (done_) return {};
        if (!opened_) {
            if (!static_cast<Derived*>(this)->open()) { done_ = true; return {}; }
            opened_ = true;
        }
        auto e = static_cast<Derived*>(this)->next();
        if (!e) { done_ = true; return {}; }
        return iris::wrap(*e);
    }
};

// ── Lazy stream implementations ───────────────────────────────────────────────

class LsStream : public OsStream<LsStream, DirEntry> {
    std::string path_;
    DIR*        dir_ = nullptr;
public:
    explicit LsStream(std::string_view p) : path_(p) {}
    LsStream(const LsStream&) = delete;
    LsStream& operator=(const LsStream&) = delete;
    LsStream& operator=(LsStream&&) = delete;

    LsStream(LsStream&& o) noexcept
        : OsStream(std::move(o)), path_(std::move(o.path_)),
          dir_(std::exchange(o.dir_, nullptr)) {}

    bool                    open();
    void                    close();
    std::optional<DirEntry> next();
};

class PsStream : public OsStream<PsStream, ProcEntry> {
    DIR* proc_ = nullptr;
public:
    PsStream() = default;
    PsStream(const PsStream&) = delete;
    PsStream& operator=(const PsStream&) = delete;
    PsStream& operator=(PsStream&&) = delete;

    PsStream(PsStream&& o) noexcept
        : OsStream(std::move(o)), proc_(std::exchange(o.proc_, nullptr)) {}

    bool                     open();
    void                     close();
    std::optional<ProcEntry> next();
};

class EnvStream : public OsStream<EnvStream, EnvEntry> {
    int idx_ = 0;
public:
    EnvStream() = default;
    EnvStream(EnvStream&&) = default;
    EnvStream& operator=(EnvStream&&) = delete;
    EnvStream(const EnvStream&) = delete;
    EnvStream& operator=(const EnvStream&) = delete;

    bool                    open()  { return true; }
    void                    close() {}
    std::optional<EnvEntry> next();
};

} // namespace iris::os

namespace iris {

// ── OsBackend — variant wrapper over lazy streams ─────────────────────────────

class OsBackend {
    std::variant<iris::os::LsStream, iris::os::PsStream, iris::os::EnvStream> stream_;

    template<typename Stream>
    explicit OsBackend(Stream s) : stream_(std::move(s)) {}

public:
    static OsBackend ls(std::string_view path = ".") { return OsBackend{iris::os::LsStream{path}}; }
    static OsBackend ps()  { return OsBackend{iris::os::PsStream{}}; }
    static OsBackend env() { return OsBackend{iris::os::EnvStream{}}; }

    std::string_view runtime_name() const { return "os"; }
    bool can_handle(const TypeDescriptor& td) const {
        return td.name == "DirEntry" || td.name == "ProcEntry" || td.name == "EnvEntry";
    }
    void emit(IrisValue&&) {}
    IrisValue recv() {
        return std::visit([](auto& s){ return s.recv(); }, stream_);
    }
};

static_assert(Backend<OsBackend>);

} // namespace iris
