/// @file tests/fuzz_irsh.cpp  libFuzzer entry point for lexer + parser
#include <irish/lexer/lexer.hpp>
#include <irish/parser/parser.hpp>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string_view src{reinterpret_cast<const char*>(data), size};
    iris::irsh::Lexer lexer{src};
    auto toks = lexer.tokenise();
    iris::irsh::Parser parser{toks, {}};
    (void)parser.parse();
    return 0;
}
