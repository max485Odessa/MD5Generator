#include "../core.h"
#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void reject(const std::string& json) {
    bool failed = false;
    try { md5gen::parseConfig(json); } catch (const std::exception&) { failed = true; }
    require(failed, "Invalid configuration accepted");
}
int main() {
    try {
        CreateDirectoryW(L"tests\\tmp", NULL);
        std::wstring file = md5gen::fullPath(L"tests\\tmp\\тест-файл.bin");
        md5gen::writeAtomic(file, "");
        require(md5gen::hashFile(file) == "d41d8cd98f00b204e9800998ecf8427e", "Empty file MD5");
        md5gen::writeAtomic(file, "abc");
        require(md5gen::hashFile(file) == "900150983cd24fb0d6963f7d28e17f72", "abc MD5");
        md5gen::writeAtomic(file, "message digest");
        require(md5gen::hashFile(file) == "f96b697d7cb7938d525a2f31aaf161d0", "message digest MD5");
        md5gen::writeAtomic(file, std::string(1000000, 'a'));
        require(md5gen::hashFile(file) == "7707d6ae4e027c70eea2a935c2296f21", "Streaming MD5");
        std::string binary;
        for (int i=0; i<256; ++i) binary += static_cast<char>(i);
        md5gen::writeAtomic(file, binary);
        require(md5gen::hashFile(file) == "e2c865db4162bed963bfaa9ef6ac18f0", "Binary MD5");
        HANDLE locked = CreateFileW(file.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        require(locked != INVALID_HANDLE_VALUE, "Lock test file");
        bool failed = false;
        try { md5gen::hashFile(file); } catch (const std::exception&) { failed = true; }
        CloseHandle(locked);
        require(failed, "Locked input must report an error");
        std::vector<std::wstring> paths;
        paths.push_back(file);
        paths.push_back(L"C:\\папка с пробелами\\документ.txt");
        paths.push_back(L"\\\\server\\share\\file.txt");
        require(md5gen::parseConfig(md5gen::encodeConfig(paths)) == paths, "Unicode/UNC roundtrip");
        require(md5gen::parseConfig("\xef\xbb\xbf{\"files\":[]}").empty(), "UTF-8 BOM");
        std::vector<std::wstring> escaped = md5gen::parseConfig("{\"files\":[\"C:\\\\\\u0442\\u0435\\u0441\\u0442-\\ud83d\\ude00.txt\"]}");
        require(escaped[0] == md5gen::wide("C:\\тест-😀.txt"), "Escaped Unicode and surrogate pairs");
        require(md5gen::parseConfig("{\"files\":[\"C:/test/a.txt\",\"c:\\\\test\\\\a.txt\"]}").size() == 1, "Duplicate paths");
        reject("{\"files\":[\"relative.txt\"]}");
        reject("{\"files\":[\"C:relative.txt\"]}");
        reject("{\"files\":[\"C:\\\\a\\u0000.txt\"]}");
        reject("{\"files\":[\"C:\\\\a\\ud800.txt\"]}");
        reject("{\"files\":[\"C:\\\\a\\udc00.txt\"]}");
        reject("{\"files\":[\"C:\\\\a.txt\",]}");
        reject("{\"files\":[]}trailing");
        reject("{\"files\":[],\"files\":[]}");
        reject("{\"files\":[42]}");
        reject("{\"other\":[]}");
        reject("{\"files\":[\"C:\\\\\xff.txt\"]}");
        std::wstring settings = md5gen::fullPath(L"tests\\tmp\\config.json");
        md5gen::writeAtomic(settings, md5gen::encodeConfig(paths));
        require(md5gen::parseConfig(md5gen::readFile(settings)) == paths, "Persist and reload");
        locked = CreateFileW(settings.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        require(locked != INVALID_HANDLE_VALUE, "Lock settings");
        failed = false;
        try { md5gen::writeAtomic(settings, "bad"); } catch (const std::exception&) { failed = true; }
        CloseHandle(locked);
        require(failed, "Locked destination must fail");
        require(md5gen::parseConfig(md5gen::readFile(settings)) == paths, "Failed save preserves original");
        DeleteFileW(file.c_str()); DeleteFileW(settings.c_str());
        failed = false;
        try { md5gen::hashFile(file); } catch (const std::exception&) { failed = true; }
        require(failed, "Missing input must report an error");
        std::cout << "PASS: MD5 vectors, streaming, binary, Unicode, JSON validation, persistence, errors\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
