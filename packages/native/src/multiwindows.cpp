// Native multi-window integration for one explicitly identified Manggo build.
// All entry points execute on Manggo's GUI thread. No installation files are modified.
#include <Windows.h>

#include <QtCore/QEvent>
#include <QtCore/QList>
#include <QtCore/QMetaObject>
#include <QtCore/QPoint>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtGui/QImage>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QWidget>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <vector>

namespace {

constexpr DWORD kExpectedTimestamp = 0x6ace6168;
constexpr DWORD kExpectedImageSize = 0x1936000;
constexpr std::size_t kWindowSize = 0x358;
constexpr std::size_t kContextOffset = 0x28;
constexpr std::size_t kConstructorRva = 0x278080;
constexpr std::size_t kAllocatorRva = 0x45cd70;
constexpr std::size_t kShowInputRva = 0x284420;
constexpr std::size_t kShowInputCallRva = 0x282d40;
constexpr std::size_t kShowSelectionRva = 0x284b30;
constexpr std::size_t kShowScreenshotRva = 0x284d40;
constexpr std::size_t kWindowEventRva = 0x27ea00;
constexpr std::size_t kCloseScheduleRva = 0x284200;
constexpr std::size_t kCloseTimerRva = 0x27fa40;
constexpr std::size_t kJumpSize = 14;
constexpr const char* kWindowClass = "manggo::ui::windows::TranslationWindow";
constexpr const char* kWindowPrefix = "multiWindowsWindow";

using ShowInput = void (*)(QWidget*);
using ShowSelection = void (*)(QWidget*, const QString*, const QPoint*);
using ShowScreenshot = void (*)(QWidget*, const QString*, const QPoint*, const QImage*);
using WindowEvent = bool (*)(QWidget*, QEvent*);
using CloseTimer = void (*)(int, void*);

HMODULE module_handle = nullptr;
HHOOK active_hook = nullptr;
int requested_action = 0;
DWORD last_error = 0;

bool matches_build() {
  HMODULE executable = GetModuleHandleW(L"Manggo.exe");
  if (executable == nullptr) return false;
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(executable);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
      reinterpret_cast<const unsigned char*>(executable) + dos->e_lfanew);
  return nt->Signature == IMAGE_NT_SIGNATURE &&
         nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
         nt->FileHeader.TimeDateStamp == kExpectedTimestamp &&
         nt->OptionalHeader.SizeOfImage == kExpectedImageSize;
}

bool is_translation_window(QWidget* window) {
  return window != nullptr && window->metaObject() != nullptr &&
         std::strcmp(window->metaObject()->className(), kWindowClass) == 0;
}

QWidget* source_window(QWidget* widget) {
  while (widget != nullptr && !is_translation_window(widget)) {
    widget = widget->parentWidget();
  }
  return widget;
}

QWidget* find_original_window() {
  const QList<QWidget*> windows = QApplication::topLevelWidgets();
  for (QWidget* window : windows) {
    if (is_translation_window(window) &&
        !window->objectName().startsWith(QLatin1String(kWindowPrefix))) {
      return window;
    }
  }
  return nullptr;
}

void write_absolute_jump(unsigned char* destination, const void* target) {
  destination[0] = 0xff;
  destination[1] = 0x25;
  *reinterpret_cast<DWORD*>(destination + 2) = 0;
  *reinterpret_cast<void**>(destination + 6) = const_cast<void*>(target);
}

class InlineHook {
 public:
  InlineHook() = default;
  InlineHook(const InlineHook&) = delete;
  InlineHook& operator=(const InlineHook&) = delete;

  bool install(std::size_t rva, const std::vector<unsigned char>& prologue,
               std::size_t resume_offset, const void* detour) {
    if (installed_ || !matches_build() || prologue.size() > resume_offset ||
        resume_offset < kJumpSize) {
      return false;
    }
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(L"Manggo.exe"));
    target_ = base + rva;
    if (std::memcmp(target_, prologue.data(), prologue.size()) != 0) return false;

    trampoline_ = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, prologue.size() + 32, MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
    if (trampoline_ == nullptr) return false;
    std::memcpy(trampoline_, prologue.data(), prologue.size());
    write_absolute_jump(trampoline_ + prologue.size(), target_ + resume_offset);
    FlushInstructionCache(GetCurrentProcess(), trampoline_, prologue.size() + 32);

    std::memcpy(original_patch_, target_, kJumpSize);
    DWORD old_protect = 0;
    if (!VirtualProtect(target_, kJumpSize, PAGE_EXECUTE_READWRITE, &old_protect)) {
      VirtualFree(trampoline_, 0, MEM_RELEASE);
      trampoline_ = nullptr;
      target_ = nullptr;
      return false;
    }
    write_absolute_jump(target_, detour);
    DWORD ignored = 0;
    VirtualProtect(target_, kJumpSize, old_protect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), target_, kJumpSize);
    installed_ = true;
    return true;
  }

  bool install_close_timer(std::size_t rva,
                           const std::array<unsigned char, 28>& prologue,
                           const void* detour) {
    if (installed_ || !matches_build()) return false;
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(L"Manggo.exe"));
    target_ = base + rva;
    if (std::memcmp(target_, prologue.data(), prologue.size()) != 0) return false;

    trampoline_ = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, 80, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (trampoline_ == nullptr) return false;

    // Rebuild the two conditional branches with absolute jumps. The detour is
    // installed at the real function entry, so every return has a balanced stack.
    std::memcpy(trampoline_, prologue.data(), 9);
    trampoline_[9] = 0x74;   // je action_zero
    trampoline_[10] = 0x17;
    std::memcpy(trampoline_ + 11, prologue.data() + 15, 3);
    trampoline_[14] = 0x75;  // jne other_action
    trampoline_[15] = 0x20;
    std::memcpy(trampoline_ + 16, prologue.data() + 24, 4);
    write_absolute_jump(trampoline_ + 20, base + rva + 0x1c);
    write_absolute_jump(trampoline_ + 34, base + rva + 0xef);
    write_absolute_jump(trampoline_ + 48, base + rva + 0xd4);
    FlushInstructionCache(GetCurrentProcess(), trampoline_, 80);

    std::memcpy(original_patch_, target_, kJumpSize);
    DWORD old_protect = 0;
    if (!VirtualProtect(target_, kJumpSize, PAGE_EXECUTE_READWRITE, &old_protect)) {
      VirtualFree(trampoline_, 0, MEM_RELEASE);
      trampoline_ = nullptr;
      target_ = nullptr;
      return false;
    }
    write_absolute_jump(target_, detour);
    DWORD ignored = 0;
    VirtualProtect(target_, kJumpSize, old_protect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), target_, kJumpSize);
    installed_ = true;
    return true;
  }

  bool install_input(std::size_t rva, const std::vector<unsigned char>& prefix,
                     const std::array<unsigned char, 5>& original_call,
                     std::size_t callee_rva, const void* detour) {
    if (installed_ || !matches_build() || prefix.size() != 11) return false;
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(L"Manggo.exe"));
    target_ = base + rva;
    if (std::memcmp(target_, prefix.data(), prefix.size()) != 0 ||
        std::memcmp(target_ + prefix.size(), original_call.data(), original_call.size()) != 0) {
      return false;
    }

    trampoline_ = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (trampoline_ == nullptr) return false;
    std::memcpy(trampoline_, prefix.data(), prefix.size());
    trampoline_[11] = 0xff;
    trampoline_[12] = 0x15;
    *reinterpret_cast<DWORD*>(trampoline_ + 13) = 14;
    void* call_target = base + callee_rva;
    std::memcpy(trampoline_ + 31, &call_target, sizeof(call_target));
    write_absolute_jump(trampoline_ + 17, target_ + 16);
    FlushInstructionCache(GetCurrentProcess(), trampoline_, 64);

    std::memcpy(original_patch_, target_, kJumpSize);
    DWORD old_protect = 0;
    if (!VirtualProtect(target_, kJumpSize, PAGE_EXECUTE_READWRITE, &old_protect)) {
      VirtualFree(trampoline_, 0, MEM_RELEASE);
      trampoline_ = nullptr;
      target_ = nullptr;
      return false;
    }
    write_absolute_jump(target_, detour);
    DWORD ignored = 0;
    VirtualProtect(target_, kJumpSize, old_protect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), target_, kJumpSize);
    installed_ = true;
    return true;
  }

  void remove() {
    if (!installed_) return;
    DWORD old_protect = 0;
    if (VirtualProtect(target_, kJumpSize, PAGE_EXECUTE_READWRITE, &old_protect)) {
      std::memcpy(target_, original_patch_, kJumpSize);
      DWORD ignored = 0;
      VirtualProtect(target_, kJumpSize, old_protect, &ignored);
      FlushInstructionCache(GetCurrentProcess(), target_, kJumpSize);
    }
    installed_ = false;
    target_ = nullptr;
    if (trampoline_ != nullptr) {
      VirtualFree(trampoline_, 0, MEM_RELEASE);
      trampoline_ = nullptr;
    }
  }

  bool installed() const { return installed_; }
  void* trampoline() const { return trampoline_; }
  unsigned char* target() const { return target_; }

 private:
  unsigned char* target_ = nullptr;
  unsigned char* trampoline_ = nullptr;
  unsigned char original_patch_[kJumpSize] = {};
  bool installed_ = false;
};

class WindowFactory {
 public:
  QWidget* create(QWidget* source) {
    source = source_window(source);
    if (source == nullptr || !matches_build()) return nullptr;
    void* context = *reinterpret_cast<void**>(
        reinterpret_cast<unsigned char*>(source) + kContextOffset);
    if (context == nullptr) return nullptr;
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(L"Manggo.exe"));
    using Allocate = void* (*)(std::size_t);
    using Construct = void* (*)(void*, void*, QWidget*);
    const auto allocate = reinterpret_cast<Allocate>(base + kAllocatorRva);
    const auto construct = reinterpret_cast<Construct>(base + kConstructorRva);
    void* storage = allocate(kWindowSize);
    if (storage == nullptr) return nullptr;
    auto* window = static_cast<QWidget*>(construct(storage, context, nullptr));
    if (window == nullptr) return nullptr;
    window->setObjectName(QLatin1String(kWindowPrefix) + QString::number(++created_total_));
    window->setAttribute(Qt::WA_DeleteOnClose, true);
    if (!install_button(window)) {
      delete window;
      return nullptr;
    }
    windows_.append(window);
    return window;
  }

  bool install_button(QWidget* window) {
    if (!is_translation_window(window)) return false;
    auto* title_bar = window->findChild<QFrame*>(QLatin1String("resultTitleBar"));
    auto* layout = title_bar == nullptr
                       ? nullptr
                       : qobject_cast<QHBoxLayout*>(title_bar->layout());
    if (title_bar == nullptr || layout == nullptr) return false;
    if (title_bar->findChild<QToolButton*>(QLatin1String("multiWindowsAddButton"),
                                           Qt::FindDirectChildrenOnly) != nullptr) {
      return true;
    }
    auto* button = new QToolButton(title_bar);
    button->setObjectName(QLatin1String("multiWindowsAddButton"));
    button->setText(QLatin1String("+"));
    button->setToolTip(QString::fromUtf8("新建独立翻译窗口"));
    button->setAutoRaise(true);
    QWidget* close = title_bar->findChild<QWidget*>(QLatin1String("resultCloseButton"),
                                                    Qt::FindDirectChildrenOnly);
    const int index = layout->indexOf(close);
    layout->insertWidget(index < 0 ? layout->count() : index, button);
    buttons_.append(button);
    QObject::connect(button, &QToolButton::clicked, button, [this, button]() {
      QWidget* source = source_window(button);
      QWidget* window = create(source);
      if (window != nullptr) {
        if (source != nullptr) window->move(source->pos() + QPoint(36, 36));
        window->show();
        window->raise();
        window->activateWindow();
      }
    });
    return true;
  }

  void remove_all() {
    auto windows = windows_;
    windows_.clear();
    for (const QPointer<QWidget>& window : windows) {
      if (!window.isNull()) delete window.data();
    }
    auto buttons = buttons_;
    buttons_.clear();
    for (const QPointer<QToolButton>& button : buttons) {
      if (!button.isNull()) delete button.data();
    }
  }

  int created_total() const { return created_total_; }
  const QList<QPointer<QWidget>>& windows() const { return windows_; }

 private:
  QList<QPointer<QWidget>> windows_;
  QList<QPointer<QToolButton>> buttons_;
  int created_total_ = 0;
};

WindowFactory factory;
InlineHook input_hook;
InlineHook selection_hook;
InlineHook screenshot_hook;
InlineHook window_event_hook;
InlineHook close_timer_hook;
ShowInput original_show_input = nullptr;
ShowSelection original_show_selection = nullptr;
ShowScreenshot original_show_screenshot = nullptr;
WindowEvent original_window_event = nullptr;
CloseTimer original_close_timer = nullptr;

void show_input_detour(QWidget* source) {
  QWidget* window = factory.create(source);
  original_show_input(window == nullptr ? source : window);
}

void show_selection_detour(QWidget* source, const QString* text, const QPoint* position) {
  QWidget* window = factory.create(source);
  original_show_selection(window == nullptr ? source : window, text, position);
}

void show_screenshot_detour(QWidget* source, const QString* text, const QPoint* position,
                            const QImage* image) {
  QWidget* window = factory.create(source);
  original_show_screenshot(window == nullptr ? source : window, text, position, image);
}

bool window_event_detour(QWidget* source, QEvent* event) {
  const bool result = original_window_event(source, event);
  if (!is_translation_window(source) || event->type() != QEvent::WindowDeactivate) {
    return result;
  }

  // Manggo skips this scheduling entirely for a pinned source. Schedule the
  // whole group so a pinned last-active window cannot strand its unpinned peers.
  // The native scheduler de-duplicates through window+0x246.
  using ScheduleClose = void (*)(QWidget*);
  auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(L"Manggo.exe"));
  const auto schedule = reinterpret_cast<ScheduleClose>(base + kCloseScheduleRva);
  const QList<QWidget*> windows = QApplication::topLevelWidgets();
  for (QWidget* window : windows) {
    if (is_translation_window(window)) schedule(window);
  }
  return result;
}

void close_timer_detour(int action, void* context) {
  if (action != 1) {
    original_close_timer(action, context);
    return;
  }

  auto* source = *reinterpret_cast<QWidget**>(
      static_cast<unsigned char*>(context) + 0x10);
  QWidget* active = QApplication::activeWindow();
  const bool foreground_is_translation =
      is_translation_window(active) &&
      reinterpret_cast<HWND>(active->winId()) == GetForegroundWindow();
  if (is_translation_window(source) && foreground_is_translation) {
    // The native body clears this pending-close flag before deciding whether
    // to hide. Do the same when another translation window has taken focus.
    // Checking the actual foreground HWND also handles switching to another
    // application before Qt has cleared its own active-window pointer.
    *reinterpret_cast<unsigned char*>(
        reinterpret_cast<unsigned char*>(source) + 0x246) = 0;
    return;
  }

  // Outside the group, the native body preserves the user's focus-out setting,
  // per-window pin state, active state, and popup/modal protection.
  original_close_timer(action, context);
}

bool install_hooks() {
  if (!input_hook.install_input(
          kShowInputRva,
          {0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xec, 0x60, 0x48, 0x8b, 0xf1},
          {0xe8, 0x10, 0xe9, 0xff, 0xff}, kShowInputCallRva,
          reinterpret_cast<const void*>(&show_input_detour))) {
    return false;
  }
  if (!selection_hook.install(
          kShowSelectionRva,
          {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74,
           0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x50},
          15, reinterpret_cast<const void*>(&show_selection_detour))) {
    input_hook.remove();
    return false;
  }
  if (!screenshot_hook.install(
          kShowScreenshotRva,
          {0x48, 0x83, 0xec, 0x38, 0x4c, 0x89, 0x4c, 0x24,
           0x20, 0x41, 0xb9, 0x04, 0x00, 0x00, 0x00},
          15, reinterpret_cast<const void*>(&show_screenshot_detour))) {
    selection_hook.remove();
    input_hook.remove();
    return false;
  }
  if (!close_timer_hook.install_close_timer(
          kCloseTimerRva,
          {0x48, 0x83, 0xec, 0x38, 0x4c, 0x8b, 0xc2, 0x85, 0xc9, 0x0f,
           0x84, 0xe0, 0x00, 0x00, 0x00, 0x83, 0xf9, 0x01, 0x0f, 0x85,
           0xbc, 0x00, 0x00, 0x00, 0x48, 0x8b, 0x42, 0x10},
          reinterpret_cast<const void*>(&close_timer_detour))) {
    screenshot_hook.remove();
    selection_hook.remove();
    input_hook.remove();
    return false;
  }
  if (!window_event_hook.install(
          kWindowEventRva,
          {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83,
           0xec, 0x20, 0x66, 0x83, 0x7a, 0x08, 0x19},
          15, reinterpret_cast<const void*>(&window_event_detour))) {
    close_timer_hook.remove();
    screenshot_hook.remove();
    selection_hook.remove();
    input_hook.remove();
    return false;
  }
  original_show_input = reinterpret_cast<ShowInput>(input_hook.trampoline());
  original_show_selection = reinterpret_cast<ShowSelection>(selection_hook.trampoline());
  original_show_screenshot = reinterpret_cast<ShowScreenshot>(screenshot_hook.trampoline());
  original_window_event = reinterpret_cast<WindowEvent>(window_event_hook.trampoline());
  original_close_timer = reinterpret_cast<CloseTimer>(close_timer_hook.trampoline());
  return true;
}

void remove_hooks() {
  window_event_hook.remove();
  close_timer_hook.remove();
  screenshot_hook.remove();
  selection_hook.remove();
  input_hook.remove();
  original_window_event = nullptr;
  original_close_timer = nullptr;
  original_show_screenshot = nullptr;
  original_show_selection = nullptr;
  original_show_input = nullptr;
}

std::FILE* open_report() {
  wchar_t path[MAX_PATH];
  if (GetModuleFileNameW(module_handle, path, MAX_PATH) == 0) return nullptr;
  wchar_t* slash = std::wcsrchr(path, L'\\');
  if (slash == nullptr) return nullptr;
  swprintf_s(slash + 1, MAX_PATH - static_cast<std::size_t>(slash + 1 - path),
             L"multiwindows-status.txt");
  std::FILE* output = nullptr;
  return _wfopen_s(&output, path, L"wb") == 0 ? output : nullptr;
}

DWORD initialize() {
  if (!matches_build()) return 10;
  if (input_hook.installed() || selection_hook.installed() || screenshot_hook.installed() ||
      window_event_hook.installed() || close_timer_hook.installed()) {
    return 11;
  }
  QWidget* original = find_original_window();
  if (original == nullptr) return 12;
  if (!factory.install_button(original)) return 14;
  if (!install_hooks()) {
    remove_hooks();
    factory.remove_all();
    return 13;
  }
  return 0;
}

void inspect(std::FILE* output) {
  int alive = 0;
  int visible = 0;
  for (const QPointer<QWidget>& window : factory.windows()) {
    if (window.isNull()) continue;
    ++alive;
    if (window->isVisible()) ++visible;
  }
  std::fprintf(output,
               "build_matched=%d input_hook=%d selection_hook=%d screenshot_hook=%d "
               "window_event_hook=%d close_timer_hook=%d created_total=%d alive=%d visible=%d\n",
               matches_build(), input_hook.installed(), selection_hook.installed(),
               screenshot_hook.installed(), window_event_hook.installed(),
               close_timer_hook.installed(), factory.created_total(), alive, visible);
}

void run_action() {
  DWORD status = 0;
  if (requested_action == 0) {
    status = initialize();
  } else if (requested_action == 1) {
    remove_hooks();
    factory.remove_all();
  }
  last_error = status;
  if (std::FILE* output = open_report()) {
    if (requested_action == 2) inspect(output);
    std::fprintf(output, "action=%d status=%lu\n", requested_action, status);
    std::fclose(output);
  }
}

LRESULT CALLBACK message_callback(int code, WPARAM wparam, LPARAM lparam) {
  if (code == HC_ACTION && active_hook != nullptr) {
    UnhookWindowsHookEx(active_hook);
    active_hook = nullptr;
    run_action();
  }
  return CallNextHookEx(nullptr, code, wparam, lparam);
}

DWORD run_on_gui_thread(HWND window, int action) {
  if (active_hook != nullptr || window == nullptr || !IsWindow(window)) return 2;
  const DWORD thread_id = GetWindowThreadProcessId(window, nullptr);
  if (thread_id == 0) return 3;
  requested_action = action;
  active_hook = SetWindowsHookExW(WH_CALLWNDPROC, message_callback, module_handle, thread_id);
  if (active_hook == nullptr) return 4;
  SendMessageW(window, WM_NULL, 0, 0);
  if (active_hook != nullptr) {
    UnhookWindowsHookEx(active_hook);
    active_hook = nullptr;
    return 5;
  }
  return last_error;
}

}  // namespace

extern "C" __declspec(dllexport) DWORD WINAPI InitializeMultiWindows(void* parameter) {
  return run_on_gui_thread(static_cast<HWND>(parameter), 0);
}
extern "C" __declspec(dllexport) DWORD WINAPI ShutdownMultiWindows(void* parameter) {
  return run_on_gui_thread(static_cast<HWND>(parameter), 1);
}
extern "C" __declspec(dllexport) DWORD WINAPI InspectMultiWindows(void* parameter) {
  return run_on_gui_thread(static_cast<HWND>(parameter), 2);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    module_handle = instance;
    DisableThreadLibraryCalls(instance);
  }
  return TRUE;
}
