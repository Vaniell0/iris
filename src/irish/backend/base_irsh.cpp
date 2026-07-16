/// @file src/irish/backend/base_irsh.cpp
/// Dispatch layer over base_ops/*.cpp. Every @base op has its own
/// translation unit under base_ops/; this file just routes op names
/// to the corresponding check_ / gen_ pair.
#include "base_irsh.hpp"
#include "base_ops/common.hpp"

namespace iris::irsh {

IrType BaseIrshBackend::check(std::string_view op,
                               const BackendConfig& config,
                               const IrType& input,
                               const TypeRegistry& global,
                               std::vector<TypeError>& errs,
                               Loc loc) const {
    using namespace base_ops;
    if (op == "filter")  return check_filter (config, input, global, session_, errs, loc);
    if (op == "sort")    return check_sort   (config, input, global, session_, errs, loc);
    if (op == "select")  return check_select (config, input, global, session_, errs, loc);
    if (op == "head")    return check_head   (config, input, global, session_, errs, loc);
    if (op == "map")     return check_map    (config, input, global, session_, errs, loc);
    if (op == "collect") return check_collect(config, input, global, session_, errs, loc);
    if (op == "lit")     return check_lit    (config, input, global, session_, errs, loc);
    if (op == "types")   return check_types  (config, input, global, session_, errs, loc);
    if (op == "type")    return check_type   (config, input, global, session_, errs, loc);
    if (op == "parse")   return check_parse  (config, input, global, session_, errs, loc);
    if (op == "print" || op == "write")
                         return check_passthrough(config, input, global, session_, errs, loc);
    errs.push_back({loc, "@base." + std::string{op} + ": unknown operation"});
    return VoidType{};
}

IrisGen BaseIrshBackend::make_gen(std::string_view op,
                                    const BackendConfig& config,
                                    const TypeDescriptor* desc,
                                    IrisGen upstream) {
    using namespace base_ops;
    if (op == "filter")  return gen_filter (config, desc, std::move(upstream), global_, session_);
    if (op == "sort")    return gen_sort   (config, desc, std::move(upstream), global_, session_);
    if (op == "select")  return gen_select (config, desc, std::move(upstream), global_, session_);
    if (op == "head")    return gen_head   (config, desc, std::move(upstream), global_, session_);
    if (op == "map")     return gen_map    (config, desc, std::move(upstream), global_, session_);
    if (op == "collect") return gen_collect(config, desc, std::move(upstream), global_, session_);
    if (op == "lit")     return gen_lit    (config, desc, std::move(upstream), global_, session_);
    if (op == "types")   return gen_types  (config, desc, std::move(upstream), global_, session_);
    if (op == "type")    return gen_type   (config, desc, std::move(upstream), global_, session_);
    if (op == "parse")   return gen_parse  (config, desc, std::move(upstream), global_, session_);
    if (op == "print" || op == "write")
                         return gen_passthrough(config, desc, std::move(upstream), global_, session_);
    return []() -> IrisResult { return iris_end(); };
}

} // namespace iris::irsh
