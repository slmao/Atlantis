// Real Android/NDK implementation of the Atlantis Platform lifecycle
// interface declared in platform.h. Per ADR-0077 (native entry point/
// process model) and ADR-0079 (ANativeWindow reference management),
// mirrors windows_platform.cpp's own isolation-boundary discipline: this
// is the only file in this module (besides android_app_injection.h/.cpp,
// which is deliberately NDK-header-free for host-testability) that
// includes an Android NDK header, and no such header, type, or macro
// appears anywhere in src/platform's public headers.
//
// android_main itself is NOT implemented here (ADR-0077/ADR-0080): it
// lives in atlantis_runtime_android (Plan 0034 Milestone 5), which
// injects its own android_app* via setAndroidApp() before calling into
// atlantis::platform's shared interface -- this file only ever
// *consumes* that injected pointer (via android_app_injection.h's
// androidApp()), never obtains its own.
#include <atlantis/platform/platform.h>

#include <atlantis/assert.h>

#include "android_app_injection.h"
#include "app_command.h"
#include "app_command_mapping.h"

#include <cstdint>
#include <iterator>
#include <vector>

#include <android/looper.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

// Confirms this file's module-private AppCommand mirror (app_command.h)
// still matches the real NDK ABI it was copied from -- see app_command.h's
// own top comment for why a separate, NDK-header-free mirror exists at
// all. A mismatch here fails this build, not a silent mistranslation.
static_assert(static_cast<std::int32_t>(atlantis::platform::android_detail::AppCommand::InputChanged) ==
              APP_CMD_INPUT_CHANGED);
static_assert(static_cast<std::int32_t>(atlantis::platform::android_detail::AppCommand::InitWindow) ==
              APP_CMD_INIT_WINDOW);
static_assert(static_cast<std::int32_t>(atlantis::platform::android_detail::AppCommand::TermWindow) ==
              APP_CMD_TERM_WINDOW);
static_assert(static_cast<std::int32_t>(atlantis::platform::android_detail::AppCommand::WindowResized) ==
              APP_CMD_WINDOW_RESIZED);
static_assert(static_cast<std::int32_t>(atlantis::platform::android_detail::AppCommand::WindowRedrawNeeded) ==
              APP_CMD_WINDOW_REDRAW_NEEDED);
static_assert(static_cast<std::int32_t>(atlantis::platform::android_detail::AppCommand::ContentRectChanged) ==
              APP_CMD_CONTENT_RECT_CHANGED);
static_assert(static_cast<std::int32_t>(atlantis::platform::android_detail::AppCommand::GainedFocus) ==
              APP_CMD_GAINED_FOCUS);
static_assert(static_cast<std::int32_t>(atlantis::platform::android_detail::AppCommand::LostFocus) ==
              APP_CMD_LOST_FOCUS);
static_assert(static_cast<std::int32_t>(atlantis::platform::android_detail::AppCommand::ConfigChanged) ==
              APP_CMD_CONFIG_CHANGED);
static_assert(static_cast<std::int32_t>(atlantis::platform::android_detail::AppCommand::LowMemory) ==
              APP_CMD_LOW_MEMORY);
static_assert(static_cast<std::int32_t>(atlantis::platform::android_detail::AppCommand::Start) == APP_CMD_START);
static_assert(static_cast<std::int32_t>(atlantis::platform::android_detail::AppCommand::Resume) == APP_CMD_RESUME);
static_assert(static_cast<std::int32_t>(atlantis::platform::android_detail::AppCommand::SaveState) ==
              APP_CMD_SAVE_STATE);
static_assert(static_cast<std::int32_t>(atlantis::platform::android_detail::AppCommand::Pause) == APP_CMD_PAUSE);
static_assert(static_cast<std::int32_t>(atlantis::platform::android_detail::AppCommand::Stop) == APP_CMD_STOP);
static_assert(static_cast<std::int32_t>(atlantis::platform::android_detail::AppCommand::Destroy) == APP_CMD_DESTROY);

namespace atlantis::platform {

namespace {

// File-local implementation state -- mirrors windows_platform.cpp's own
// State struct shape (pendingBuffer/outputBuffer double-buffering keeps
// a span returned by a previous processEvents() call valid until the
// next processEvents()/shutdown() call, per platform.h's documented
// contract; a synchronous onAppCmd dispatch landing in pendingBuffer
// between calls can never invalidate an already-returned span the same
// way Windows' own synchronous window-message dispatch cannot).
struct State {
  bool initialized = false;
  bool shutDown = false;
  bool quit = false;
  std::vector<PlatformEvent> pendingBuffer;
  std::vector<PlatformEvent> outputBuffer;
};

State& state() {
  static State instance;
  return instance;
}

void movePendingIntoOutput(State& s) {
  s.outputBuffer.insert(s.outputBuffer.end(), std::make_move_iterator(s.pendingBuffer.begin()),
                         std::make_move_iterator(s.pendingBuffer.end()));
  s.pendingBuffer.clear();
}

// Wired as android_app::onAppCmd by initialize() below -- invoked
// synchronously by android_native_app_glue's own process_cmd(), from
// inside this call's own ALooper_pollOnce() drain loop in
// processEvents() (never from any other thread -- ADR-0004's
// single-application-thread baseline applies to whichever thread
// android_main's own event loop runs on, per ADR-0077's own
// Negative/Trade-offs note; implementers must not confuse it with the
// Java UI thread NativeActivity itself still runs).
void onAppCmd(struct android_app* app, std::int32_t cmd) {
  State& s = state();
  const auto command = static_cast<android_detail::AppCommand>(cmd);

  android_detail::AppCommandContext context;
  if (command == android_detail::AppCommand::InitWindow) {
    // ADR-0079: app->window is already the new, valid ANativeWindow* by
    // the time this callback fires for InitWindow -- android_native_app_glue's
    // own android_app_pre_exec_cmd() sets it before invoking onAppCmd
    // (confirmed by reading android_native_app_glue.c). Stored verbatim,
    // no acquire/release.
    context.nativeWindow = app->window;
  } else if (command == android_detail::AppCommand::WindowResized ||
             command == android_detail::AppCommand::ContentRectChanged) {
    if (app->window != nullptr) {
      context.width = static_cast<unsigned int>(ANativeWindow_getWidth(app->window));
      context.height = static_cast<unsigned int>(ANativeWindow_getHeight(app->window));
    }
  }

  const std::optional<PlatformEvent> mapped = android_detail::mapAppCommand(command, context);
  if (mapped.has_value()) {
    s.pendingBuffer.push_back(*mapped);
  }
  // ADR-0077's own "observed and discarded" row (extended by this file's
  // own AppCommand::WindowRedrawNeeded completeness correction, see
  // app_command_mapping.h): a command with no PlatformEvent mapping is
  // silently dropped here, not asserted or logged.

  if (command == android_detail::AppCommand::Destroy) {
    // android_native_app_glue's own android_app_pre_exec_cmd() already
    // set app->destroyRequested = 1 before this callback fires -- this
    // flag is Atlantis Platform's own, independent mirror of that fact,
    // read by shouldQuit() exactly like Windows Platform's quit flag.
    s.quit = true;
  }
}

}  // namespace

atlantis::Result<std::monostate, PlatformError> initialize() {
  State& s = state();

  s.outputBuffer.clear();
  s.pendingBuffer.clear();
  s.quit = false;
  s.shutDown = false;

  struct android_app* app = android_detail::androidApp();
  app->onAppCmd = &onAppCmd;

  s.initialized = true;

  // Unlike Windows (which synthesizes SurfaceCreated once around
  // CreateWindowExW, per Spec 0002's own Ownership and Lifetime table),
  // Android's real SurfaceCreated is not synthesized here: the framework
  // may not have posted APP_CMD_INIT_WINDOW yet at this point (Android's
  // asynchronous, framework-driven model -- ADR-0012), so it arrives
  // through the very next processEvents() call's own drain loop instead,
  // exactly the same way every other post-initialize() PlatformEvent
  // does. This is not a Windows/Android inconsistency needing
  // reconciliation: Spec 0002 never required SurfaceCreated to be
  // observable from initialize() itself, only that it precede any use of
  // the handle it carries.
  return atlantis::Result<std::monostate, PlatformError>::Ok(std::monostate{});
}

std::span<const PlatformEvent> processEvents() {
  State& s = state();
  ATLANTIS_CHECK_MSG(s.initialized, "processEvents() called before a successful initialize()");

  s.outputBuffer.clear();
  movePendingIntoOutput(s);

  struct android_app* app = android_detail::androidApp();

  // Non-blocking (timeoutMillis = 0), matching platform.h's documented
  // "drains and returns this call's events" contract. Uses
  // ALooper_pollOnce, not ALooper_pollAll: confirmed by reading the NDK's
  // own <android/looper.h> (NDK 29.0.14206865) that ALooper_pollAll
  // carries __REMOVED_IN(1, ...), making it unusable at any API level
  // this repository could target -- a technical correction to
  // ADR-0077's own Decision text, which named ALooper_pollAll
  // illustratively; a looped ALooper_pollOnce achieves the identical
  // "drain everything currently pending, non-blocking" semantics that
  // Decision actually requires. The android_app::cmdPollSource/
  // inputPollSource fds were registered via ALooper_addFd() with a data
  // pointer and no callback (android_native_app_glue.c's own
  // android_app_entry()), so a >= 0 return here means outData holds that
  // android_poll_source* for the caller (this function) to process
  // itself, per ALooper_pollOnce's own documented contract.
  int events = 0;
  void* data = nullptr;
  while (ALooper_pollOnce(0, nullptr, &events, &data) >= 0) {
    if (data != nullptr) {
      auto* source = static_cast<struct android_poll_source*>(data);
      source->process(app, source);
    }
  }

  movePendingIntoOutput(s);

  return std::span<const PlatformEvent>{s.outputBuffer.data(), s.outputBuffer.size()};
}

bool shouldQuit() {
  ATLANTIS_CHECK_MSG(state().initialized, "shouldQuit() called before a successful initialize()");
  return state().quit;
}

void shutdown() {
  State& s = state();
  ATLANTIS_CHECK_MSG(s.initialized && !s.shutDown,
                      "shutdown() called without a successful initialize(), or called twice");

  // Explicitly invalidate the batch from the last processEvents() call,
  // per platform.h's documented contract.
  s.outputBuffer.clear();

  // Per ADR-0013/ADR-0079: Android Platform never destroys the native
  // window, the ANativeActivity, or the android_app structure itself --
  // the framework (via android_native_app_glue) owns all three. Nothing
  // to release here; onAppCmd is simply never invoked again once the
  // framework tears android_main's own thread down.
  s.shutDown = true;

  // Re-initialization after shutdown() is unsupported in Phase 1,
  // mirroring windows_platform.cpp's own identical, already-documented
  // limitation (Spec 0002 Section 6 / Unresolved Implementation Details
  // #7): not designed, not guarded.
}

PlatformKind currentPlatform() { return PlatformKind::Android; }

}  // namespace atlantis::platform
