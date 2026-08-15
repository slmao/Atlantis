#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "command_line_quoting.h"

namespace atlantis::tools::shader_compiler::detail {

std::wstring utf8ToWide(const std::string& utf8) {
  if (utf8.empty()) return {};
  const int wideLength = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), wideLength);
  return wide;
}

std::wstring quoteArgument(const std::wstring& arg) {
  const bool needsQuoting = arg.empty() || arg.find_first_of(L" \t\"") != std::wstring::npos;
  if (!needsQuoting) return arg;

  std::wstring result = L"\"";
  std::size_t backslashRun = 0;
  for (const wchar_t c : arg) {
    if (c == L'\\') {
      ++backslashRun;
      continue;
    }
    if (c == L'"') {
      result.append(backslashRun * 2, L'\\');
      backslashRun = 0;
      result += L"\\\"";
      continue;
    }
    result.append(backslashRun, L'\\');
    backslashRun = 0;
    result.push_back(c);
  }
  result.append(backslashRun * 2, L'\\');
  result += L'"';
  return result;
}

std::wstring buildCommandLine(const std::vector<std::string>& arguments) {
  std::wstring commandLine;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    if (i != 0) commandLine += L' ';
    commandLine += quoteArgument(utf8ToWide(arguments[i]));
  }
  return commandLine;
}

}  // namespace atlantis::tools::shader_compiler::detail
