#ifndef MD5GEN_CORE_H
#define MD5GEN_CORE_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif
#include <windows.h>
#include <string>
#include <vector>

namespace md5gen {
std::string utf8(const std::wstring& value);
std::wstring wide(const std::string& value);
std::wstring systemError(DWORD code);
std::wstring fullPath(const std::wstring& path);
bool absolutePath(const std::wstring& path);
bool samePath(const std::wstring& a, const std::wstring& b);
std::wstring baseName(const std::wstring& path);
std::vector<std::wstring> parseConfig(const std::string& json);
std::string encodeConfig(const std::vector<std::wstring>& files);
std::string readFile(const std::wstring& path);
void writeAtomic(const std::wstring& path, const std::string& bytes);
std::string hashFile(const std::wstring& path);
}
#endif
