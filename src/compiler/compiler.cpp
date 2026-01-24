#include <chip8/compiler/compiler.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>

namespace chip8 {
namespace compiler {

Compiler::Compiler()
    : debug_mode(false),
      parser(std::make_unique<Parser>(errorHandler)),
      codeGen(std::make_unique<CodeGenerator>(errorHandler)) {
}

void Compiler::compile(const std::string &source_file, const std::string &output_file) {
    try {
        std::ifstream input(source_file);
        if (!input) {
            errorHandler.error("Could not open source file " + source_file, 0, 0);
            return;
        }

        std::stringstream buffer;
        buffer << input.rdbuf();
        std::string source = buffer.str();
        input.close();

        if (debug_mode) {
            std::cout << "Compiling " << source_file << " to " << output_file << std::endl;
        }
        
        errorHandler.setSource(source);

        std::vector<Token> tokens = lexer.tokenize(source);

        if (debug_mode) {
            std::cout << "Tokenization complete: " << tokens.size() << " tokens" << std::endl;

            int count = std::min(10, static_cast<int>(tokens.size()));
            for (int i = 0; i < count; i++) {
                std::cout << "Token " << i << ": "
                          << static_cast<int>(tokens[i].type) << " - "
                          << tokens[i].value << std::endl;
            }
        }

        auto ast = parser->parse(tokens);

        if (errorHandler.hasErrors()) {
            for (const auto& error : errorHandler.getErrors()) {
                std::cerr << error.format();
            }
            return;
        }

        if (debug_mode) {
            std::cout << "Parsing complete" << std::endl;
        }

        std::vector<uint8_t> output = codeGen->generate(*ast);

        if (errorHandler.hasErrors()) {
            for (const auto& error : errorHandler.getErrors()) {
                std::cerr << error.format();
            }
            return;
        }
        
        if (errorHandler.hasWarnings()) {
            for (const auto& warning : errorHandler.getWarnings()) {
                std::cerr << warning.filename;
                if (warning.line > 0) std::cerr << ":" << warning.line;
                if (warning.column > 0) std::cerr << ":" << warning.column;
                std::cerr << ": warning: " << warning.message << "\n";
                if (!warning.source_line.empty()) {
                    std::cerr << "    " << warning.source_line << "\n";
                    if (warning.column > 0) {
                        std::cerr << "    " << std::string(warning.column - 1, ' ') << "^\n";
                    }
                }
            }
        }

        if (debug_mode) {
            std::cout << "Code generation complete: " << output.size() << " bytes" << std::endl;

            int count = std::min(20, static_cast<int>(output.size()));
            for (int i = 0; i < count; i += 2) {
                if (i + 1 < count) {
                    std::cout << "0x" << std::hex << std::setw(4) << std::setfill('0')
                              << ((output[i] << 8) | output[i+1]) << std::dec << " ";
                }
            }
            std::cout << std::endl;
        }

        std::ofstream out(output_file, std::ios::binary);
        if (!out) {
            errorHandler.error("Could not open output file " + output_file, 0, 0);
            return;
        }

        out.write(reinterpret_cast<const char*>(output.data()), output.size());

        if (debug_mode) {
            std::cout << "Compilation successful: wrote " << output.size() << " bytes to "
                      << output_file << std::endl;
        }

    }
    catch (const std::exception& e) {
        errorHandler.error(std::string("Compilation failed: ") + e.what(), 0, 0);
    }
}

}
}
