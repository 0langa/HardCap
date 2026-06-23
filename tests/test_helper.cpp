#include <windows.h>

#include <cstdlib>
#include <new>
#include <string>
#include <vector>

int wmain(int argc, wchar_t** argv) {
    if (argc >= 3 && std::wstring_view(argv[1]) == L"allocate") {
        const auto mib = std::wcstoull(argv[2], nullptr, 10);
        try {
            std::vector<std::byte> memory(static_cast<size_t>(mib * 1024ULL * 1024ULL));
            for (size_t i = 0; i < memory.size(); i += 4096) memory[i] = std::byte{1};
            Sleep(30000);
            return 0;
        } catch (const std::bad_alloc&) {
            return 42;
        }
    }
    if (argc >= 2 && std::wstring_view(argv[1]) == L"spin") {
        volatile unsigned long long value = 1;
        const ULONGLONG end = GetTickCount64() + 30000;
        while (GetTickCount64() < end) value = value * 1664525ULL + 1013904223ULL;
        return static_cast<int>(value & 0);
    }
    return 0;
}
