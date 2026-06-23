#include "core/rule_repository.h"

#include <windows.h>
#include <shlobj.h>

#include <charconv>
#include <chrono>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace hardcap {
namespace {

std::string to_utf8(const std::wstring_view value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) throw std::runtime_error("UTF-8 conversion failed");
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring from_utf8(const std::string_view value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) throw std::runtime_error("Invalid UTF-8");
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        result.data(), size);
    return result;
}

std::string quote(const std::wstring_view value) {
    const std::string utf8 = to_utf8(value);
    std::string out{"\""};
    for (const unsigned char c : utf8) {
        switch (c) {
        case '\"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                constexpr char digits[] = "0123456789abcdef";
                out += "\\u00";
                out += digits[c >> 4];
                out += digits[c & 0x0f];
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += '\"';
    return out;
}

class Parser {
public:
    explicit Parser(std::string_view input) : input_(input) {}

    std::vector<Rule> parse_document() {
        expect('{');
        std::vector<Rule> rules;
        bool schema_seen = false;
        while (!consume('}')) {
            const auto key = parse_string();
            expect(':');
            if (key == "schemaVersion") {
                if (parse_unsigned() != 1) throw std::runtime_error("Unsupported settings version");
                schema_seen = true;
            } else if (key == "rules") {
                rules = parse_rules();
            } else {
                skip_value();
            }
            if (consume('}')) break;
            expect(',');
        }
        skip_space();
        if (position_ != input_.size() || !schema_seen) throw std::runtime_error("Invalid settings document");
        return rules;
    }

private:
    std::vector<Rule> parse_rules() {
        expect('[');
        std::vector<Rule> rules;
        if (consume(']')) return rules;
        while (true) {
            rules.push_back(parse_rule());
            if (consume(']')) break;
            expect(',');
        }
        return rules;
    }

    Rule parse_rule() {
        expect('{');
        Rule rule{};
        bool path_seen = false;
        if (consume('}')) throw std::runtime_error("Empty rule");
        while (true) {
            const auto key = parse_string();
            expect(':');
            if (key == "id") rule.id = from_utf8(parse_string());
            else if (key == "executablePath") { rule.executable_path = from_utf8(parse_string()); path_seen = true; }
            else if (key == "displayName") rule.display_name = from_utf8(parse_string());
            else if (key == "enabled") rule.enabled = parse_bool();
            else if (key == "cpuEnabled") rule.cpu_enabled = parse_bool();
            else if (key == "cpuPercent") rule.cpu_percent = static_cast<std::uint32_t>(parse_unsigned());
            else if (key == "memoryEnabled") rule.memory_enabled = parse_bool();
            else if (key == "memoryBytes") rule.memory_bytes = parse_unsigned();
            else skip_value();
            if (consume('}')) break;
            expect(',');
        }
        if (!path_seen || rule.executable_path.empty()) throw std::runtime_error("Rule path is missing");
        if (rule.id.empty()) rule.id = create_rule_id();
        rule.executable_path = normalize_executable_path(rule.executable_path);
        if (validate_rule(rule, 0)) throw std::runtime_error("Invalid rule limits");
        return rule;
    }

    std::string parse_string() {
        skip_space();
        if (position_ >= input_.size() || input_[position_++] != '\"') throw std::runtime_error("Expected string");
        std::string out;
        while (position_ < input_.size()) {
            const char c = input_[position_++];
            if (c == '\"') return out;
            if (c != '\\') { out += c; continue; }
            if (position_ >= input_.size()) throw std::runtime_error("Bad escape");
            const char escaped = input_[position_++];
            switch (escaped) {
            case '\"': out += '\"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                if (position_ + 4 > input_.size()) throw std::runtime_error("Bad unicode escape");
                unsigned value = 0;
                for (int i = 0; i < 4; ++i) {
                    const char h = input_[position_++];
                    value = value * 16 + (h >= '0' && h <= '9' ? h - '0' :
                        h >= 'a' && h <= 'f' ? h - 'a' + 10 :
                        h >= 'A' && h <= 'F' ? h - 'A' + 10 : throw std::runtime_error("Bad unicode escape"));
                }
                const wchar_t wide = static_cast<wchar_t>(value);
                out += to_utf8(std::wstring_view(&wide, 1));
                break;
            }
            default: throw std::runtime_error("Bad escape");
            }
        }
        throw std::runtime_error("Unterminated string");
    }

    std::uint64_t parse_unsigned() {
        skip_space();
        const size_t start = position_;
        while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
        std::uint64_t value{};
        if (start == position_ || std::from_chars(input_.data() + start, input_.data() + position_, value).ec != std::errc{}) {
            throw std::runtime_error("Expected unsigned integer");
        }
        return value;
    }

    bool parse_bool() {
        skip_space();
        if (input_.substr(position_, 4) == "true") { position_ += 4; return true; }
        if (input_.substr(position_, 5) == "false") { position_ += 5; return false; }
        throw std::runtime_error("Expected Boolean");
    }

    void skip_value() {
        skip_space();
        if (position_ >= input_.size()) throw std::runtime_error("Missing value");
        if (input_[position_] == '\"') { static_cast<void>(parse_string()); return; }
        if (input_[position_] >= '0' && input_[position_] <= '9') { static_cast<void>(parse_unsigned()); return; }
        if (input_.substr(position_, 4) == "true" || input_.substr(position_, 5) == "false") { static_cast<void>(parse_bool()); return; }
        const char open = input_[position_];
        if (open != '{' && open != '[') throw std::runtime_error("Unsupported value");
        const char close = open == '{' ? '}' : ']';
        int depth = 0;
        bool in_string = false;
        bool escape = false;
        do {
            const char c = input_[position_++];
            if (in_string) {
                if (escape) escape = false;
                else if (c == '\\') escape = true;
                else if (c == '\"') in_string = false;
            } else if (c == '\"') in_string = true;
            else if (c == open) ++depth;
            else if (c == close) --depth;
        } while (position_ < input_.size() && depth > 0);
        if (depth != 0) throw std::runtime_error("Unclosed value");
    }

    void skip_space() { while (position_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[position_]))) ++position_; }
    bool consume(const char expected) { skip_space(); if (position_ < input_.size() && input_[position_] == expected) { ++position_; return true; } return false; }
    void expect(const char expected) { if (!consume(expected)) throw std::runtime_error("Unexpected JSON token"); }

    std::string_view input_;
    size_t position_{0};
};

std::wstring message_for_last_error(const wchar_t* prefix) {
    return std::wstring(prefix) + L" (Windows error " + std::to_wstring(GetLastError()) + L").";
}

} // namespace

RuleRepository::RuleRepository(std::filesystem::path path) : path_(std::move(path)) {}

RuleLoadResult RuleRepository::load() const {
    if (!std::filesystem::exists(path_)) return {};
    try {
        std::ifstream stream(path_, std::ios::binary);
        if (!stream) throw std::runtime_error("Cannot open settings");
        const std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        return {Parser(content).parse_document(), {}};
    } catch (...) {
        const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        auto invalid = path_;
        invalid += L".invalid-" + std::to_wstring(stamp) + L".json";
        std::error_code ignored;
        std::filesystem::rename(path_, invalid, ignored);
        return {{}, L"Settings were invalid and moved to " + invalid.filename().wstring() + L"."};
    }
}

std::optional<std::wstring> RuleRepository::save(const std::vector<Rule>& rules) const {
    try {
        if (path_.has_parent_path()) std::filesystem::create_directories(path_.parent_path());
        std::ostringstream json;
        json << "{\n  \"schemaVersion\": 1,\n  \"rules\": [";
        for (size_t i = 0; i < rules.size(); ++i) {
            const Rule& rule = rules[i];
            json << (i == 0 ? "\n" : ",\n")
                 << "    {\"id\":" << quote(rule.id)
                 << ",\"executablePath\":" << quote(rule.executable_path)
                 << ",\"displayName\":" << quote(rule.display_name)
                 << ",\"enabled\":" << (rule.enabled ? "true" : "false")
                 << ",\"cpuEnabled\":" << (rule.cpu_enabled ? "true" : "false")
                 << ",\"cpuPercent\":" << rule.cpu_percent
                 << ",\"memoryEnabled\":" << (rule.memory_enabled ? "true" : "false")
                 << ",\"memoryBytes\":" << rule.memory_bytes << '}';
        }
        json << (rules.empty() ? "" : "\n") << "  ]\n}\n";

        auto temporary = path_;
        temporary += L".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream) return L"Could not create the temporary settings file.";
            const std::string content = json.str();
            stream.write(content.data(), static_cast<std::streamsize>(content.size()));
            stream.flush();
            if (!stream) return L"Could not write the settings file.";
        }
        if (!MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temporary.c_str());
            return message_for_last_error(L"Could not replace the settings file");
        }
        return std::nullopt;
    } catch (const std::exception&) {
        return L"Could not save settings.";
    }
}

std::filesystem::path RuleRepository::default_path() {
    PWSTR local_app_data = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local_app_data))) {
        return L"settings.json";
    }
    std::filesystem::path path(local_app_data);
    CoTaskMemFree(local_app_data);
    return path / L"HardCap" / L"settings.json";
}

} // namespace hardcap
