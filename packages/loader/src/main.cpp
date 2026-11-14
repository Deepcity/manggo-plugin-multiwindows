// Process-lifetime loader prototype for the native MultiWindows DLL.
// Does not modify Manggo files, install auto-start, or remain running after injection.
#include <Windows.h>
#include <Psapi.h>
#include <TlHelp32.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr DWORD kProcessAccess = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                 PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;

struct WindowSearch {
  DWORD pid;
  HWND fallback;
  HWND visible;
};

std::wstring module_directory() {
  std::vector<wchar_t> path(32768);
  const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (length == 0 || length >= path.size()) return {};
  std::wstring result(path.data(), length);
  const auto slash = result.find_last_of(L"\\/");
  return slash == std::wstring::npos ? std::wstring{} : result.substr(0, slash + 1);
}

std::wstring quote_argument(const std::wstring& value) {
  std::wstring result = L"\"";
  for (wchar_t ch : value) {
    if (ch == L'\"') result += L'\\';
    result += ch;
  }
  result += L'\"';
  return result;
}

DWORD find_manggo_process() {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return 0;
  PROCESSENTRY32W entry = {};
  entry.dwSize = sizeof(entry);
  DWORD pid = 0;
  if (Process32FirstW(snapshot, &entry)) {
    do {
      if (_wcsicmp(entry.szExeFile, L"Manggo.exe") == 0) {
        pid = entry.th32ProcessID;
        break;
      }
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return pid;
}

BOOL CALLBACK enum_window(HWND window, LPARAM parameter) {
  auto* search = reinterpret_cast<WindowSearch*>(parameter);
  DWORD pid = 0;
  GetWindowThreadProcessId(window, &pid);
  if (pid != search->pid) return TRUE;
  if (search->fallback == nullptr) search->fallback = window;
  if (IsWindowVisible(window) && search->visible == nullptr) search->visible = window;
  return search->visible == nullptr;
}

HWND find_manggo_window(DWORD pid) {
  WindowSearch search = {pid, nullptr, nullptr};
  EnumWindows(&enum_window, reinterpret_cast<LPARAM>(&search));
  return search.visible != nullptr ? search.visible : search.fallback;
}

DWORD launch_manggo(const std::wstring& executable) {
  std::wstring command = quote_argument(executable);
  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process = {};
  if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
                      nullptr, nullptr, &startup, &process)) {
    return 0;
  }
  CloseHandle(process.hThread);
  const DWORD pid = process.dwProcessId;
  CloseHandle(process.hProcess);
  return pid;
}

DWORD wait_for_window(DWORD pid) {
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (find_manggo_window(pid) != nullptr) return 0;
    Sleep(100);
  }
  return ERROR_TIMEOUT;
}

DWORD export_rva(const std::wstring& path, const char* export_name) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return 0;
  const DWORD size = GetFileSize(file, nullptr);
  std::vector<unsigned char> data(size);
  DWORD read = 0;
  const bool ok = size > 0 && ReadFile(file, data.data(), size, &read, nullptr) && read == size;
  CloseHandle(file);
  if (!ok) return 0;

  const auto u16 = [&](std::size_t offset) -> WORD {
    if (offset + 2 > data.size()) return 0;
    WORD value = 0;
    std::memcpy(&value, data.data() + offset, 2);
    return value;
  };
  const auto u32 = [&](std::size_t offset) -> DWORD {
    if (offset + 4 > data.size()) return 0;
    DWORD value = 0;
    std::memcpy(&value, data.data() + offset, 4);
    return value;
  };
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(data.data());
  if (data.size() < sizeof(IMAGE_DOS_HEADER) || dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
  const DWORD pe = dos->e_lfanew;
  if (pe + 24 > data.size() || u32(pe) != IMAGE_NT_SIGNATURE) return 0;
  const WORD sections_count = u16(pe + 6);
  const WORD optional_size = u16(pe + 20);
  const DWORD optional = pe + 24;
  if (u16(optional) != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return 0;
  const DWORD export_directory = u32(optional + 112);

  auto file_offset = [&](DWORD rva) -> DWORD {
    for (WORD i = 0; i < sections_count; ++i) {
      const DWORD section = optional + optional_size + i * 40;
      const DWORD virtual_size = u32(section + 8);
      const DWORD virtual_address = u32(section + 12);
      const DWORD raw_size = u32(section + 16);
      const DWORD raw_address = u32(section + 20);
      if (rva >= virtual_address &&
          rva < virtual_address + std::max(virtual_size, raw_size)) {
        return raw_address + rva - virtual_address;
      }
    }
    return 0;
  };

  const DWORD exports = file_offset(export_directory);
  if (exports == 0) return 0;
  const DWORD names = file_offset(u32(exports + 32));
  const DWORD ordinals = file_offset(u32(exports + 36));
  const DWORD functions = file_offset(u32(exports + 28));
  const DWORD count = u32(exports + 24);
  for (DWORD i = 0; i < count; ++i) {
    const DWORD name = file_offset(u32(names + i * 4));
    if (name == 0 || name >= data.size()) continue;
    const char* current = reinterpret_cast<const char*>(data.data() + name);
    const std::size_t remaining = data.size() - name;
    if (std::memchr(current, '\0', remaining) == nullptr) continue;
    if (std::strcmp(current, export_name) == 0) {
      return u32(functions + u16(ordinals + i * 2) * 4);
    }
  }
  return 0;
}

HMODULE remote_module(HANDLE process, const wchar_t* filename) {
  std::vector<HMODULE> modules(1024);
  DWORD needed = 0;
  if (!EnumProcessModules(process, modules.data(),
                          static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &needed)) {
    return nullptr;
  }
  const std::size_t count = std::min(modules.size(), needed / sizeof(HMODULE));
  for (std::size_t i = 0; i < count; ++i) {
    wchar_t name[MAX_PATH];
    if (GetModuleBaseNameW(process, modules[i], name, MAX_PATH) != 0 &&
        _wcsicmp(name, filename) == 0) {
      return modules[i];
    }
  }
  return nullptr;
}

DWORD call_remote(HANDLE process, const void* address, void* parameter) {
  HANDLE thread = CreateRemoteThread(process, nullptr, 0,
                                     reinterpret_cast<LPTHREAD_START_ROUTINE>(
                                         const_cast<void*>(address)),
                                     parameter, 0, nullptr);
  if (thread == nullptr) return GetLastError();
  const DWORD wait = WaitForSingleObject(thread, 15000);
  DWORD status = ERROR_TIMEOUT;
  if (wait == WAIT_OBJECT_0 && !GetExitCodeThread(thread, &status)) status = GetLastError();
  CloseHandle(thread);
  return status;
}

int run(bool unload, const std::wstring& manggo_path, const std::wstring& dll_path) {
  DWORD pid = find_manggo_process();
  if (pid == 0 && unload) {
    std::fwprintf(stderr, L"Manggo is not running.\n");
    return 2;
  }
  if (pid == 0) {
    if (manggo_path.empty()) {
      std::fwprintf(stderr, L"Manggo is not running; pass its Manggo.exe path.\n");
      return 2;
    }
    pid = launch_manggo(manggo_path);
    if (pid == 0) {
      std::fwprintf(stderr, L"Could not launch Manggo (Win32 %lu).\n", GetLastError());
      return 3;
    }
  }
  if (wait_for_window(pid) != 0) {
    std::fwprintf(stderr, L"Manggo did not create a UI window within 20 seconds.\n");
    return 4;
  }
  HWND window = find_manggo_window(pid);
  HANDLE process = OpenProcess(kProcessAccess, FALSE, pid);
  if (process == nullptr) {
    std::fwprintf(stderr, L"OpenProcess failed (Win32 %lu).\n", GetLastError());
    return 5;
  }

  int result = 0;
  HMODULE remote = remote_module(process, L"manggo-multiwindows.dll");
  if (unload) {
    if (remote == nullptr) {
      std::fwprintf(stderr, L"MultiWindows is not loaded.\n");
      result = 6;
    } else {
      const DWORD rva = export_rva(dll_path, "ShutdownMultiWindows");
      const DWORD status = rva == 0 ? ERROR_PROC_NOT_FOUND
                                    : call_remote(process, reinterpret_cast<unsigned char*>(remote) + rva,
                                                  window);
      if (status != 0) {
        std::fwprintf(stderr, L"ShutdownMultiWindows returned %lu.\n", status);
        result = 7;
      } else {
        call_remote(process, &FreeLibrary, remote);
        std::wprintf(L"MultiWindows unloaded for Manggo process %lu.\n", pid);
      }
    }
    CloseHandle(process);
    return result;
  }

  if (remote != nullptr) {
    std::fwprintf(stderr, L"MultiWindows is already loaded.\n");
    CloseHandle(process);
    return 8;
  }

  const DWORD rva = export_rva(dll_path, "InitializeMultiWindows");
  if (rva == 0) {
    CloseHandle(process);
    return 12;
  }
  std::wstring remote_path_data = dll_path + L"\0";
  for (int attempt = 0; attempt < 200; ++attempt) {
    window = find_manggo_window(pid);
    if (window == nullptr) {
      Sleep(100);
      continue;
    }
    void* remote_path = VirtualAllocEx(process, nullptr,
                                       remote_path_data.size() * sizeof(wchar_t),
                                       MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote_path == nullptr) {
      CloseHandle(process);
      return 9;
    }
    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote_path, remote_path_data.data(),
                            remote_path_data.size() * sizeof(wchar_t), &written)) {
      VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
      CloseHandle(process);
      return 10;
    }
    const DWORD load_status = call_remote(process, &LoadLibraryW, remote_path);
    VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    if (load_status == 0) {
      std::fwprintf(stderr, L"LoadLibraryW failed.\n");
      CloseHandle(process);
      return 11;
    }
    remote = remote_module(process, L"manggo-multiwindows.dll");
    if (remote == nullptr) {
      CloseHandle(process);
      return 12;
    }
    const DWORD status = call_remote(process,
                                     reinterpret_cast<unsigned char*>(remote) + rva, window);
    if (status == 0) break;
    call_remote(process, &FreeLibrary, remote);
    remote = nullptr;
    // Status 12 means the native TranslationWindow does not exist yet.
    if (status != 12 || attempt == 199) {
      std::fwprintf(stderr, L"InitializeMultiWindows returned %lu; extension unloaded.\n", status);
      CloseHandle(process);
      return 13;
    }
    Sleep(100);
  }
  std::wprintf(L"MultiWindows loaded for Manggo process %lu. Loader will now exit.\n", pid);
  CloseHandle(process);
  return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  bool unload = false;
  std::wstring manggo_path;
  for (int i = 1; i < argc; ++i) {
    if (std::wcscmp(argv[i], L"--unload") == 0) {
      unload = true;
    } else if (manggo_path.empty()) {
      manggo_path = argv[i];
    } else {
      std::fwprintf(stderr, L"Usage: manggo-multiwindows-loader.exe [--unload] [Manggo.exe path]\n");
      return 1;
    }
  }
  const std::wstring dll_path = module_directory() + L"manggo-multiwindows.dll";
  if (GetFileAttributesW(dll_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    std::fwprintf(stderr, L"Missing sibling DLL: %s\n", dll_path.c_str());
    return 14;
  }
  return run(unload, manggo_path, dll_path);
}
