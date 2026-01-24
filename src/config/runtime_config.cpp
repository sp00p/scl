#include <chip8/runtime_config.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>

#ifdef _WIN32
#define NOMINMAX  // Prevent Windows min/max macros
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#endif

namespace chip8 {

// Global config instance
static RuntimeConfig g_config;

RuntimeConfig& getConfig() {
    return g_config;
}

// Simple config file parser (INI-style for simplicity)
// Format:
// [section]
// key = value

static std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

static uint32_t parseColor(const std::string& str) {
    std::string s = trim(str);
    if (s.empty()) return 0;
    
    // Remove 0x or # prefix
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s = s.substr(2);
    } else if (s[0] == '#') {
        s = s.substr(1);
    }
    
    return static_cast<uint32_t>(std::stoul(s, nullptr, 16));
}

static bool parseBool(const std::string& str) {
    std::string s = trim(str);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s == "true" || s == "1" || s == "yes";
}

static int parseInt(const std::string& str) {
    return std::stoi(trim(str));
}

bool RuntimeConfig::load(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    std::string section;
    
    while (std::getline(file, line)) {
        line = trim(line);
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        
        // Section header
        if (line[0] == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            std::transform(section.begin(), section.end(), section.begin(), ::tolower);
            continue;
        }
        
        // Key-value pair
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        
        try {
            if (section == "quirks") {
                if (key == "vf_reset") quirks.vf_reset = parseBool(value);
                else if (key == "shift_vy") quirks.shift_vy = parseBool(value);
                else if (key == "index_increment") quirks.index_increment = parseBool(value);
                else if (key == "jump_vx") quirks.jump_vx = parseBool(value);
                else if (key == "clip_sprites") quirks.clip_sprites = parseBool(value);
                else if (key == "schip_mode") quirks.schip_mode = parseBool(value);
            }
            else if (section == "colors") {
                if (key == "background") colors.background = parseColor(value);
                else if (key == "foreground") colors.foreground = parseColor(value);
            }
            else if (section == "timing") {
                if (key == "instructions_per_frame") timing.instructions_per_frame = parseInt(value);
                else if (key == "target_fps") timing.target_fps = parseInt(value);
            }
            else if (section == "keys") {
                // Format: key_0 = 45 (SDL scancode)
                if (key.substr(0, 4) == "key_") {
                    int idx = std::stoi(key.substr(4), nullptr, 16);
                    if (idx >= 0 && idx < 16) {
                        keys.keys[idx] = parseInt(value);
                    }
                }
            }
            else if (section == "recent") {
                if (key.substr(0, 4) == "rom_") {
                    int idx = std::stoi(key.substr(4));
                    if (idx >= 0 && idx < 10 && !value.empty()) {
                        recent_roms[idx] = value;
                        if (idx >= recent_count) recent_count = idx + 1;
                    }
                }
                else if (key == "last_directory") {
                    last_directory = value;
                }
            }
        } catch (...) {
            // Ignore parse errors, use defaults
        }
    }
    
    return true;
}

bool RuntimeConfig::save(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        return false;
    }
    
    file << "# SCL Configuration File\n\n";
    
    file << "[quirks]\n";
    file << "vf_reset = " << (quirks.vf_reset ? "true" : "false") << "\n";
    file << "shift_vy = " << (quirks.shift_vy ? "true" : "false") << "\n";
    file << "index_increment = " << (quirks.index_increment ? "true" : "false") << "\n";
    file << "jump_vx = " << (quirks.jump_vx ? "true" : "false") << "\n";
    file << "clip_sprites = " << (quirks.clip_sprites ? "true" : "false") << "\n";
    file << "schip_mode = " << (quirks.schip_mode ? "true" : "false") << "\n";
    file << "\n";
    
    file << "[colors]\n";
    file << "background = 0x" << std::hex << colors.background << "\n";
    file << "foreground = 0x" << std::hex << colors.foreground << "\n";
    file << std::dec << "\n";
    
    file << "[timing]\n";
    file << "instructions_per_frame = " << timing.instructions_per_frame << "\n";
    file << "target_fps = " << timing.target_fps << "\n";
    file << "\n";
    
    file << "[keys]\n";
    for (int i = 0; i < 16; ++i) {
        file << "key_" << std::hex << i << " = " << std::dec << keys.keys[i] << "\n";
    }
    file << "\n";
    
    file << "[recent]\n";
    file << "last_directory = " << last_directory << "\n";
    for (int i = 0; i < recent_count && i < 10; ++i) {
        if (!recent_roms[i].empty()) {
            file << "rom_" << i << " = " << recent_roms[i] << "\n";
        }
    }
    
    return true;
}

std::string RuntimeConfig::getDefaultConfigPath() {
    std::string path;
    
#ifdef _WIN32
    char appdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata))) {
        path = std::string(appdata) + "\\scl\\";
        CreateDirectoryA(path.c_str(), NULL);
        path += "config.ini";
    } else {
        path = "scl.ini";
    }
#else
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    if (home) {
        path = std::string(home) + "/.config/scl/";
        // Create directory (ignore errors)
        mkdir(path.c_str(), 0755);
        path += "config.ini";
    } else {
        path = "scl.ini";
    }
#endif
    
    return path;
}

bool RuntimeConfig::loadDefault() {
    // First try current directory
    if (load("scl.ini")) {
        return true;
    }
    
    // Then try default config path
    return load(getDefaultConfigPath());
}

bool RuntimeConfig::saveDefault() const {
    return save(getDefaultConfigPath());
}

void RuntimeConfig::addRecentRom(const std::string& path) {
    // Check if already in list
    for (int i = 0; i < recent_count; ++i) {
        if (recent_roms[i] == path) {
            // Move to front
            for (int j = i; j > 0; --j) {
                recent_roms[j] = recent_roms[j - 1];
            }
            recent_roms[0] = path;
            return;
        }
    }
    
    // Add to front, shift others down
    for (int i = std::min(recent_count, 9); i > 0; --i) {
        recent_roms[i] = recent_roms[i - 1];
    }
    recent_roms[0] = path;
    if (recent_count < 10) recent_count++;
}

} // namespace chip8
