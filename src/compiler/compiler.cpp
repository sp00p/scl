#include <chip8/compiler/compiler.h>
#include <chip8/compiler/preprocessor.h>
#include <chip8/compiler/optimizer.h>
#include <chip8/disassembler.h>
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
        input.close();

        if (debug_mode) {
            std::cout << "Compiling " << source_file << " to " << output_file << std::endl;
        }

        // Preprocess (#include, #define)
        Preprocessor preprocessor;
        std::string source = preprocessor.processFile(source_file);
        if (preprocessor.hasErrors()) {
            for (const auto& err : preprocessor.getErrors()) {
                errorHandler.error(err, 0, 0);
            }
            for (const auto& error : errorHandler.getErrors()) {
                std::cerr << error.format();
            }
            return;
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

        // AST-level optimizations (constant folding, dead code elimination)
        Optimizer optimizer;
        if (optimize) {
            optimizer.optimize(*ast);

            if (debug_mode) {
                std::cout << "Optimization complete: " << optimizer.getConstantsFolded()
                          << " constants folded, " << optimizer.getDeadCodeRemoved()
                          << " dead statements removed" << std::endl;
            }
        }

        codeGen->setPeepholeEnabled(optimize);
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

        // Emit source map alongside the ROM (used by the debugger's source panel)
        compiler::SourceMap map = codeGen->getSourceMap();
        map.setSourceFile(source_file);
        std::string map_file = output_file + ".map";
        if (!map.save(map_file)) {
            errorHandler.warning("Could not write source map: " + map_file, 0, 0);
        }

        if (debug_mode) {
            std::cout << "Compilation successful: wrote " << output.size() << " bytes to "
                      << output_file << " (source map: " << map_file << ")" << std::endl;
        }

        if (show_stats) {
            constexpr size_t ROM_CAPACITY = 0xDFF - 0x200 + 1; // 3072 bytes (0x200-0xDFF)
            size_t data_bytes = codeGen->getDataBytes();
            std::cout << "--- Compilation stats ---\n";
            std::cout << "ROM size:     " << output.size() << " / " << ROM_CAPACITY
                      << " bytes (" << (output.size() * 100 / ROM_CAPACITY) << "%)\n";
            std::cout << "  code:       " << (output.size() - data_bytes) << " bytes\n";
            std::cout << "  data:       " << data_bytes << " bytes (sprites/tables)\n";
            if (optimize) {
                std::cout << "peephole:     " << codeGen->getPeepholeSaved() << " bytes removed\n";
                std::cout << "const folds:  " << optimizer.getConstantsFolded() << "\n";
            }
            std::cout << "register peaks per function (of 13 usable):\n";
            for (const auto& [name, peak] : codeGen->getFunctionRegisterPeaks()) {
                int shown = peak;
                if (shown == 0xE) shown = 13;      // VE counts as the 13th
                else if (shown > 12) shown = 12;
                std::cout << "  " << name << ": " << shown << "\n";
            }
        }

        if (emit_listing) {
            // Annotated disassembly interleaved with source lines
            std::string lst_file = output_file + ".lst";
            std::ofstream lst(lst_file);
            if (lst) {
                // First mapping address per line-start for annotation
                std::map<uint16_t, int> addr_to_line;
                for (const auto& m : map.getAllMappings()) {
                    if (!addr_to_line.count(m.address)) addr_to_line[m.address] = m.line;
                }
                map.setSource(source);

                Disassembler disasm;
                auto instructions = disasm.disassemble(output);
                lst << "; Annotated listing for " << source_file << "\n\n";
                for (const auto& instr : instructions) {
                    auto line_it = addr_to_line.find(instr.address);
                    if (line_it != addr_to_line.end()) {
                        std::string text = map.getSourceLine(line_it->second);
                        // Trim leading whitespace
                        size_t start = text.find_first_not_of(" \t");
                        if (start != std::string::npos) text = text.substr(start);
                        lst << "\n; line " << line_it->second << ": " << text << "\n";
                    }
                    lst << std::hex << std::uppercase << std::setfill('0');
                    lst << std::setw(3) << instr.address << ":  "
                        << std::setw(4) << instr.opcode << "  " << std::dec;
                    lst << instr.mnemonic;
                    if (!instr.comment.empty()) lst << "  " << instr.comment;
                    lst << "\n";
                }
                if (debug_mode) {
                    std::cout << "Listing written to " << lst_file << std::endl;
                }
            }
        }

    }
    catch (const std::exception& e) {
        errorHandler.error(std::string("Compilation failed: ") + e.what(), 0, 0);
    }
}

}
}
