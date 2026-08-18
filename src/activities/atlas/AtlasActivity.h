#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <memory>
#include <string>

#include "activities/Activity.h"
#include "atlas/AtlasConfigStore.h"
#include "atlas/AtlasFeedCache.h"
#include "network/HttpDownloader.h"
#include "util/ButtonNavigator.h"

struct Rect;
struct ThemeMetrics;

class AtlasActivity final : public Activity {
  enum class State : uint8_t {
    Ready,
    WifiSelection,
    Fetching,
  };

  struct FetchResult {
    bool ok = false;
    bool bodyTooLarge = false;
    bool aborted = false;
    atlas_feed::ParseError parseError = atlas_feed::ParseError::None;
    std::unique_ptr<atlas_feed::AtlasFeedJsonParser> parser;
  };

  ButtonNavigator buttonNavigator;
  State state = State::Ready;
  AtlasConfigStatus configStatus = AtlasConfigStatus::NotLoaded;
  atlas_feed::Feed feed{};
  bool hasFeed = false;
  bool cacheIsStale = false;
  int selectedTask = 0;
  std::string statusLine;
  std::string errorLine;
  std::string workerUrl;
  std::string workerToken;
  std::unique_ptr<FetchResult> workerResult;
  TaskHandle_t workerTask = nullptr;
  std::atomic<bool> workerDone{false};
  std::atomic<bool> workerCancel{false};
  bool networkUsed = false;

  static void workerTrampoline(void* ctx);

  void loadConfigAndCache();
  void beginRefresh();
  void onWifiSelectionComplete(bool connected);
  void startFetchWorker();
  void runFetchWorker();
  void pollFetchWorker();
  void applyFetchResult();
  void clearWorkerSecrets();
  void buildTaskSubtitle(const atlas_feed::Task& task, char* out, size_t outSize) const;
  std::string buildAgentLine(size_t index) const;
  const char* configStatusText() const;
  void renderTouchActions(const Rect& screen, const ThemeMetrics& metrics) const;
  bool handleTouchActions();

 public:
  explicit AtlasActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Atlas", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::Fetching; }
  bool skipLoopDelay() override { return state == State::Fetching; }
  bool handleHomeGesture() override;
};
