/// @file src/irish/backend/base_ops/parse.cpp
#include "common.hpp"
#include <registry.hpp>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace iris::irsh::base_ops {

IrType check_parse(const BackendConfig& config,
                   const IrType&,
                   const iris::TypeRegistry& global,
                   const Session& session,
                   std::vector<TypeError>& errs,
                   Loc loc) {
    // parse(TypeName) — TextLine → Stream<TypeName>
    if (auto* s = std::get_if<std::string>(&config)) {
        if (auto* d = global.find(*s))                     return StreamType{d->id};
        if (auto* d = session.session_types().find(*s))    return StreamType{d->id};
        errs.push_back({loc, "parse: unknown type '" + *s + "'"});
    } else {
        errs.push_back({loc, "parse: expected type name, e.g. parse(DirEntry)"});
    }
    return VoidType{};
}

IrisGen gen_parse(const BackendConfig& config,
                  const iris::TypeDescriptor*,
                  IrisGen upstream,
                  const iris::TypeRegistry& global,
                  const Session& session) {
    // Split each TextLine by whitespace → fill struct fields in field order.
    std::string tname;
    if (auto* s = std::get_if<std::string>(&config)) tname = *s;
    const iris::TypeDescriptor* td = global.find(tname);
    if (!td) td = session.session_types().find(tname);
    return [td, up = std::move(upstream)]() mutable -> IrisResult {
        while (auto _r = up()) {
            if (!*_r) return iris_end();
            if (!td) continue;
            std::string line;
            if ((*_r)->is_str()) line = std::get<std::string>((*_r)->payload);
            else continue;
            std::vector<std::string> tokens;
            std::istringstream iss{line};
            for (std::string tok; iss >> tok;) tokens.push_back(std::move(tok));
            if (tokens.empty()) continue;
            std::vector<uint8_t> buf(td->total_size, 0);
            for (size_t i = 0; i < td->fields.size() && i < tokens.size(); ++i) {
                auto& f = td->fields[i];
                auto dst = buf.data() + f.offset;
                switch (f.kind) {
                    case iris::PrimitiveKind::Bool: {
                        uint8_t b = (tokens[i] == "true" || tokens[i] == "1") ? 1 : 0;
                        std::memcpy(dst, &b, 1);
                    } break;
                    case iris::PrimitiveKind::I8: {
                        int8_t n = static_cast<int8_t>(std::strtol(tokens[i].c_str(), nullptr, 10));
                        std::memcpy(dst, &n, 1);
                    } break;
                    case iris::PrimitiveKind::I16: {
                        int16_t n = static_cast<int16_t>(std::strtol(tokens[i].c_str(), nullptr, 10));
                        std::memcpy(dst, &n, 2);
                    } break;
                    case iris::PrimitiveKind::I32: {
                        int32_t n = static_cast<int32_t>(std::strtol(tokens[i].c_str(), nullptr, 10));
                        std::memcpy(dst, &n, 4);
                    } break;
                    case iris::PrimitiveKind::I64: {
                        int64_t n = std::strtoll(tokens[i].c_str(), nullptr, 10);
                        std::memcpy(dst, &n, std::min<size_t>(8, f.size));
                    } break;
                    case iris::PrimitiveKind::F64: {
                        double d = std::strtod(tokens[i].c_str(), nullptr);
                        std::memcpy(dst, &d, std::min<size_t>(8, f.size));
                    } break;
                    case iris::PrimitiveKind::F32: {
                        float d = std::strtof(tokens[i].c_str(), nullptr);
                        std::memcpy(dst, &d, std::min<size_t>(4, f.size));
                    } break;
                    case iris::PrimitiveKind::Str:
                    case iris::PrimitiveKind::CStr: {
                        // Fixed-size C-string stored in buffer (read_field reads via strnlen)
                        size_t cap = f.size > 0 ? f.size : tokens[i].size() + 1;
                        size_t len = std::min(tokens[i].size(), cap > 0 ? cap - 1 : 0);
                        std::memcpy(dst, tokens[i].data(), len);
                        if (cap > len) reinterpret_cast<char*>(dst)[len] = '\0';
                    } break;
                    default: break;
                }
            }
            iris::IrisValue out;
            out.type_id = td->id;
            out.payload = iris::IrisBuffer::from(buf.data(), buf.size());
            return iris_val(std::move(out));
        }
        return iris_end();
    };
}

} // namespace iris::irsh::base_ops
