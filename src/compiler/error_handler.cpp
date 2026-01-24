#include <chip8/compiler/error_handler.h>
#include <sstream>
#include <algorithm>

namespace chip8 {
namespace compiler {

std::string CompilationError::format() const {
    std::ostringstream ss;
    
    if (!filename.empty()) {
        ss << filename << ":";
    }
    if (line > 0) {
        ss << line << ":";
        if (column > 0) {
            ss << column << ":";
        }
    }
    ss << " error: " << message << "\n";
    
    if (!source_line.empty() && line > 0) {
        ss << "    " << source_line << "\n";
        if (column > 0) {
            ss << "    " << std::string(column - 1, ' ') << "^\n";
        }
    }
    
    return ss.str();
}

CompilationException::CompilationException(const CompilationError& err)
    : std::runtime_error(err.format()), error(err) {}

ErrorHandler::ErrorHandler(bool throwOnError)
    : throwOnError(throwOnError) {
    errors.clear();
    warnings.clear();
}

void ErrorHandler::setSource(const std::string& source) {
    source_lines.clear();
    std::istringstream stream(source);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        source_lines.push_back(line);
    }
}

std::string ErrorHandler::getSourceLine(int line) const {
    if (line > 0 && static_cast<size_t>(line) <= source_lines.size()) {
        return source_lines[line - 1];
    }
    return "";
}

void ErrorHandler::error(const std::string& message, int line, int column, const std::string& filename) {
    std::string src_line = getSourceLine(line);
    CompilationError err{message, line, column, filename, src_line};
    errors.push_back(err);
    if (throwOnError) {
        throw CompilationException(err);
    }
}

void ErrorHandler::warning(const std::string& message, int line, int column, const std::string& filename) {
    std::string src_line = getSourceLine(line);
    warnings.emplace_back(message, line, column, filename, src_line);
}

void ErrorHandler::reset() {
    errors.clear();
    warnings.clear();
    source_lines.clear();
}

}
}
