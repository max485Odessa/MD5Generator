#include "core.h"
#include <wincrypt.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <limits>

namespace md5gen {
namespace {
struct Handle {
    HANDLE value;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
private:
    Handle(const Handle&);
    Handle& operator=(const Handle&);
};
void fail(const char* operation) {
    DWORD code = GetLastError();
    throw std::runtime_error(std::string(operation) + ": " + utf8(systemError(code)));
}
void appendUtf8(std::string& text, unsigned code) {
    if (code < 0x80) text += static_cast<char>(code);
    else if (code < 0x800) {
        text += static_cast<char>(0xc0 | (code >> 6));
        text += static_cast<char>(0x80 | (code & 63));
    } else if (code < 0x10000) {
        text += static_cast<char>(0xe0 | (code >> 12));
        text += static_cast<char>(0x80 | ((code >> 6) & 63));
        text += static_cast<char>(0x80 | (code & 63));
    } else {
        text += static_cast<char>(0xf0 | (code >> 18));
        text += static_cast<char>(0x80 | ((code >> 12) & 63));
        text += static_cast<char>(0x80 | ((code >> 6) & 63));
        text += static_cast<char>(0x80 | (code & 63));
    }
}
// Deliberately strict schema: one object with one "files" array of strings.
class Parser {
    const std::string& s;
    size_t pos;
    void bad() const { throw std::runtime_error("Invalid config.json near byte " + std::to_string(pos + 1)); }
    void ws() { while (pos < s.size() && (s[pos]==' ' || s[pos]=='\t' || s[pos]=='\r' || s[pos]=='\n')) ++pos; }
    bool take(char c) { ws(); if (pos < s.size() && s[pos] == c) { ++pos; return true; } return false; }
    void expect(char c) { if (!take(c)) bad(); }
    unsigned hex4() {
        unsigned n = 0;
        for (int i=0; i<4; ++i) {
            if (pos == s.size()) bad();
            char c = s[pos++];
            n <<= 4;
            if (c >= '0' && c <= '9') n += c-'0';
            else if (c >= 'a' && c <= 'f') n += c-'a'+10;
            else if (c >= 'A' && c <= 'F') n += c-'A'+10;
            else bad();
        }
        return n;
    }
    std::string str() {
        expect('"');
        std::string result;
        while (pos < s.size()) {
            unsigned char c = static_cast<unsigned char>(s[pos++]);
            if (c == '"') { wide(result); return result; }
            if (c < 32) bad();
            if (c != '\\') { result += static_cast<char>(c); continue; }
            if (pos == s.size()) bad();
            c = static_cast<unsigned char>(s[pos++]);
            switch (c) {
            case '"': case '\\': case '/': result += static_cast<char>(c); break;
            case 'b': result += '\b'; break;
            case 'f': result += '\f'; break;
            case 'n': result += '\n'; break;
            case 'r': result += '\r'; break;
            case 't': result += '\t'; break;
            case 'u': {
                unsigned code = hex4();
                if (code >= 0xd800 && code <= 0xdbff) {
                    if (pos+2 > s.size() || s[pos] != '\\' || s[pos+1] != 'u') bad();
                    pos += 2;
                    unsigned low = hex4();
                    if (low < 0xdc00 || low > 0xdfff) bad();
                    code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                } else if (code >= 0xdc00 && code <= 0xdfff) bad();
                appendUtf8(result, code);
                break;
            }
            default: bad();
            }
        }
        bad(); return result;
    }
public:
    explicit Parser(const std::string& text) : s(text), pos(0) {
        if (s.compare(0, 3, "\xef\xbb\xbf") == 0) pos = 3;
    }
    std::vector<std::wstring> parse() {
        std::vector<std::wstring> files;
        expect('{');
        if (str() != "files") bad();
        expect(':'); expect('[');
        if (!take(']')) {
            do {
                std::wstring path = wide(str());
                if (!absolutePath(path)) throw std::runtime_error("Every entry in files must be an absolute Windows file path.");
                path = fullPath(path);
                bool duplicate = false;
                for (size_t i=0; i<files.size(); ++i) if (samePath(files[i], path)) duplicate = true;
                if (!duplicate) files.push_back(path);
            } while (take(','));
            expect(']');
        }
        expect('}'); ws();
        if (pos != s.size()) bad();
        return files;
    }
};
}

std::string utf8(const std::wstring& value) {
    if (value.empty()) return "";
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), NULL, 0, NULL, NULL);
    if (!count) throw std::runtime_error("Invalid UTF-16 text");
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), &result[0], count, NULL, NULL);
    return result;
}
std::wstring wide(const std::string& value) {
    if (value.empty()) return L"";
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), NULL, 0);
    if (!count) throw std::runtime_error("Invalid UTF-8 text");
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), &result[0], count);
    return result;
}
std::wstring systemError(DWORD code) {
    wchar_t* buffer = NULL;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           NULL, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, NULL);
    std::wstring result = n ? std::wstring(buffer, n) : L"Windows error " + std::to_wstring(code);
    if (buffer) LocalFree(buffer);
    while (!result.empty() && (result.back()==L'\r' || result.back()==L'\n' || result.back()==L' ')) result.pop_back();
    return result;
}
bool absolutePath(const std::wstring& p) {
    if (p.find_first_of(L"\r\n\t") != std::wstring::npos || p.find(L'\0') != std::wstring::npos) return false;
    if (p.size() >= 3 && ((p[0]>=L'A' && p[0]<=L'Z') || (p[0]>=L'a' && p[0]<=L'z')) && p[1]==L':' && (p[2]==L'\\' || p[2]==L'/')) return true;
    if (p.size() > 4 && p[0]==L'\\' && p[1]==L'\\' && p[2]!=L'.' && p[2]!=L'?') {
        size_t slash = p.find(L'\\', 2);
        return slash != std::wstring::npos && slash > 2 && slash+1 < p.size();
    }
    return false;
}
std::wstring fullPath(const std::wstring& path) {
    DWORD count = GetFullPathNameW(path.c_str(), 0, NULL, NULL);
    if (!count) fail("GetFullPathName");
    std::vector<wchar_t> buffer(count);
    DWORD n = GetFullPathNameW(path.c_str(), count, &buffer[0], NULL);
    if (!n || n >= count) fail("GetFullPathName");
    return std::wstring(&buffer[0], n);
}
bool samePath(const std::wstring& a, const std::wstring& b) {
    std::wstring aa = fullPath(a), bb = fullPath(b);
    if (CompareStringOrdinal(aa.c_str(), -1, bb.c_str(), -1, TRUE) == CSTR_EQUAL) return true;
    Handle ha(CreateFileW(aa.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
    Handle hb(CreateFileW(bb.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
    BY_HANDLE_FILE_INFORMATION ia = {}, ib = {};
    return ha.value != INVALID_HANDLE_VALUE && hb.value != INVALID_HANDLE_VALUE && GetFileInformationByHandle(ha.value, &ia) && GetFileInformationByHandle(hb.value, &ib)
        && ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber && ia.nFileIndexHigh == ib.nFileIndexHigh && ia.nFileIndexLow == ib.nFileIndexLow;
}
std::wstring baseName(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos+1);
}
std::vector<std::wstring> parseConfig(const std::string& json) { return Parser(json).parse(); }
std::string encodeConfig(const std::vector<std::wstring>& files) {
    std::string result = "{\n  \"files\": [";
    for (size_t i=0; i<files.size(); ++i) {
        result += i ? ",\n    \"" : "\n    \"";
        std::string path = utf8(files[i]);
        for (size_t j=0; j<path.size(); ++j) {
            unsigned char c = static_cast<unsigned char>(path[j]);
            if (c == '\\' || c == '"') result += '\\';
            if (c < 32) throw std::runtime_error("Control characters in path");
            result += static_cast<char>(c);
        }
        result += '"';
    }
    result += files.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return result;
}
std::string readFile(const std::wstring& path) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
    if (file.value == INVALID_HANDLE_VALUE) fail("Open settings");
    std::string result;
    char buffer[8192]; DWORD n;
    for (;;) {
        if (!ReadFile(file.value, buffer, sizeof(buffer), &n, NULL)) fail("Read settings");
        if (!n) break;
        result.append(buffer, n);
        if (result.size() > 16*1024*1024) throw std::runtime_error("Settings exceed 16 MiB");
    }
    return result;
}
void writeAtomic(const std::wstring& path, const std::string& bytes) {
    std::wstring target = fullPath(path);
    std::wstring directory = target.substr(0, target.find_last_of(L'\\')+1);
    wchar_t temp[MAX_PATH];
    if (!GetTempFileNameW(directory.c_str(), L"md5", 0, temp)) fail("Create temporary file");
    try {
        {
            Handle file(CreateFileW(temp, GENERIC_WRITE, 0, NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
            if (file.value == INVALID_HANDLE_VALUE) fail("Open output");
            size_t pos = 0;
            while (pos < bytes.size()) {
                DWORD n = 0, chunk = static_cast<DWORD>((bytes.size()-pos > 65536) ? 65536 : bytes.size()-pos);
                if (!WriteFile(file.value, bytes.data()+pos, chunk, &n, NULL) || n == 0) fail("Write output");
                pos += n;
            }
            if (!FlushFileBuffers(file.value)) fail("Flush output");
        }
        if (!MoveFileExW(temp, target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) fail("Replace output");
    } catch (...) { DeleteFileW(temp); throw; }
}
std::string hashFile(const std::wstring& path) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
    if (file.value == INVALID_HANDLE_VALUE) fail("Open file");
    HCRYPTPROV provider = 0;
    if (!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) fail("CryptAcquireContext");
    HCRYPTHASH hash = 0;
    try {
        if (!CryptCreateHash(provider, CALG_MD5, 0, 0, &hash)) fail("CryptCreateHash");
        BYTE buffer[65536]; DWORD n;
        for (;;) {
            if (!ReadFile(file.value, buffer, sizeof(buffer), &n, NULL)) fail("Read file");
            if (!n) break;
            if (!CryptHashData(hash, buffer, n, 0)) fail("CryptHashData");
        }
        BYTE digest[16]; DWORD size = sizeof(digest);
        if (!CryptGetHashParam(hash, HP_HASHVAL, digest, &size, 0)) fail("CryptGetHashParam");
        std::ostringstream out;
        for (unsigned i=0; i<size; ++i) out << std::hex << std::setfill('0') << std::setw(2) << static_cast<unsigned>(digest[i]);
        CryptDestroyHash(hash); CryptReleaseContext(provider, 0);
        return out.str();
    } catch (...) {
        if (hash) CryptDestroyHash(hash);
        CryptReleaseContext(provider, 0);
        throw;
    }
}
}
