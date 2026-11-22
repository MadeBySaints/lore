#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <string>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int) {
  wchar_t module[32768];
  if (!GetModuleFileNameW(nullptr, module, 32768)) return 1;
  const std::wstring path(module);
  const std::wstring folder = path.substr(0, path.find_last_of(L"\\/"));
  const std::wstring binary = folder + L"\\lore.exe";
  std::wstring command = L"\"" + binary + L"\" open " + arguments;

  // The desktop entry starts the same app as the CLI without a console window.
  // Keep Qt startup diagnostics available if a damaged installation fails.
  SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE log = INVALID_HANDLE_VALUE;
  PWSTR local = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
    const std::wstring logs = std::wstring(local) + L"\\Lore";
    CreateDirectoryW(logs.c_str(), nullptr);
    log = CreateFileW((logs + L"\\startup.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    CoTaskMemFree(local);
  }
  HANDLE input = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  STARTUPINFOW startup{}; startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = input; startup.hStdOutput = startup.hStdError = log == INVALID_HANDLE_VALUE ? input : log;
  PROCESS_INFORMATION process{};
  SetEnvironmentVariableW(L"QT_FORCE_STDERR_LOGGING", L"1");
  const BOOL started = CreateProcessW(binary.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, folder.c_str(), &startup, &process);
  if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
  if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
  if (!started) {
    MessageBoxW(nullptr, L"Lore could not start. Reinstall Lore using the setup program so all required files are present.", L"Lore", MB_OK | MB_ICONERROR);
    return 1;
  }
  bool failed = false;
  if (WaitForSingleObject(process.hProcess, 2000) == WAIT_OBJECT_0) {
    DWORD code = 0; GetExitCodeProcess(process.hProcess, &code); failed = code != 0;
  }
  CloseHandle(process.hThread); CloseHandle(process.hProcess);
  if (failed) {
    MessageBoxW(nullptr, L"Lore closed during startup. Reinstall the app. Startup details are saved in your local Lore folder as startup.log.", L"Lore", MB_OK | MB_ICONERROR);
    return 1;
  }
  return 0;
}
