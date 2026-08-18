#include "AtlasActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>
#include <utility>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr int FETCH_WORKER_STACK = 8192;
constexpr UBaseType_t FETCH_WORKER_PRIORITY = 1;
constexpr unsigned long FETCH_EXIT_WAIT_MS = 15000UL;

void secureClear(std::string& value) {
  volatile char* data = value.empty() ? nullptr : &value[0];
  for (size_t i = 0; i < value.size(); ++i) {
    data[i] = 0;
  }
  value.clear();
}

void copyFeed(atlas_feed::Feed& dst, const atlas_feed::Feed& src) { memcpy(&dst, &src, sizeof(dst)); }

bool contains(const Rect& rect, const int x, const int y) {
  return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
}

}  // namespace

void AtlasActivity::onEnter() {
  Activity::onEnter();
  loadConfigAndCache();
  requestUpdate();
}

void AtlasActivity::onExit() {
  Activity::onExit();
  workerCancel.store(true, std::memory_order_release);
  if (workerTask != nullptr) {
    const unsigned long start = millis();
    while (!workerDone.load(std::memory_order_acquire) && millis() - start < FETCH_EXIT_WAIT_MS) {
      delay(25);
    }
    if (!workerDone.load(std::memory_order_acquire)) {
      // Never externally delete a task that may own TLS/Wi-Fi/heap locks. This
      // path is unreachable through normal Atlas navigation and is only a
      // last-resort recovery for an unexpected external activity teardown.
      LOG_ERR("ATLAS", "Atlas worker did not stop safely; restarting");
      ESP.restart();
      while (true) delay(1000);
    }
    workerTask = nullptr;
    state = State::Ready;
  }
  clearWorkerSecrets();

  if (networkUsed) {
    if (WiFi.getMode() != WIFI_MODE_NULL) {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      delay(30);
    }
    silentRestart();
  }
}

bool AtlasActivity::handleHomeGesture() {
  if (state == State::Fetching) return true;
  return false;
}

void AtlasActivity::loadConfigAndCache() {
  configStatus = ATLAS_CONFIG.load() ? AtlasConfigStatus::Ok : ATLAS_CONFIG.getStatus();
  statusLine = configStatus == AtlasConfigStatus::Ok ? tr(STR_ATLAS_READY) : configStatusText();
  errorLine.clear();

  auto parser = makeUniqueNoThrow<atlas_feed::AtlasFeedJsonParser>();
  if (!parser) {
    errorLine = tr(STR_ATLAS_MEMORY_ERROR);
    return;
  }

  if (AtlasFeedCache::load(*parser)) {
    copyFeed(feed, parser->getFeed());
    hasFeed = true;
    cacheIsStale = true;
    selectedTask = 0;
    statusLine = std::string(tr(STR_ATLAS_CACHE_LOADED)) + " - " + feed.generatedAt;
  } else {
    hasFeed = false;
    cacheIsStale = false;
    if (configStatus == AtlasConfigStatus::Ok) {
      statusLine = tr(STR_ATLAS_NO_DATA);
    }
  }
}

const char* AtlasActivity::configStatusText() const {
  switch (configStatus) {
    case AtlasConfigStatus::Ok:
      return tr(STR_ATLAS_READY);
    case AtlasConfigStatus::Missing:
      return tr(STR_ATLAS_CONFIG_MISSING);
    case AtlasConfigStatus::InvalidJson:
      return tr(STR_ATLAS_CONFIG_INVALID_JSON);
    case AtlasConfigStatus::InvalidUrl:
      return tr(STR_ATLAS_CONFIG_INVALID_URL);
    case AtlasConfigStatus::InvalidToken:
      return tr(STR_ATLAS_CONFIG_INVALID_TOKEN);
    case AtlasConfigStatus::NotLoaded:
    default:
      return tr(STR_ATLAS_CONFIG_NOT_LOADED);
  }
}

void AtlasActivity::beginRefresh() {
  if (state == State::Fetching || state == State::WifiSelection) return;

  configStatus = ATLAS_CONFIG.load() ? AtlasConfigStatus::Ok : ATLAS_CONFIG.getStatus();
  const auto config = ATLAS_CONFIG.getConfig();
  if (!config) {
    errorLine = configStatusText();
    statusLine = hasFeed ? tr(STR_ATLAS_LAST_GOOD) : tr(STR_ATLAS_NO_DATA);
    requestUpdate();
    return;
  }

  workerUrl = config->url;
  workerToken = config->token;
  networkUsed = true;
  state = State::WifiSelection;
  errorLine.clear();
  statusLine = tr(STR_CHECKING_WIFI);
  requestUpdate();

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void AtlasActivity::onWifiSelectionComplete(const bool connected) {
  state = State::Ready;
  if (!connected) {
    WiFi.scanDelete();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    errorLine = tr(STR_ATLAS_WIFI_CANCELLED);
    statusLine = hasFeed ? tr(STR_ATLAS_LAST_GOOD) : tr(STR_ATLAS_NO_DATA);
    clearWorkerSecrets();
    requestUpdate();
    return;
  }

  networkUsed = true;
  statusLine = tr(STR_ATLAS_REFRESHING);
  errorLine.clear();
  requestUpdate();
  startFetchWorker();
}

void AtlasActivity::startFetchWorker() {
  if (workerTask != nullptr) return;

  constexpr size_t parserHeadroom = sizeof(atlas_feed::AtlasFeedJsonParser) + 4096U;
  if (ESP.getMaxAllocHeap() < parserHeadroom) {
    errorLine = tr(STR_ATLAS_MEMORY_ERROR);
    statusLine = hasFeed ? tr(STR_ATLAS_LAST_GOOD) : tr(STR_ATLAS_NO_DATA);
    clearWorkerSecrets();
    requestUpdate();
    return;
  }

  workerResult = makeUniqueNoThrow<FetchResult>();
  if (workerResult) workerResult->parser = makeUniqueNoThrow<atlas_feed::AtlasFeedJsonParser>();
  if (!workerResult || !workerResult->parser) {
    errorLine = tr(STR_ATLAS_MEMORY_ERROR);
    statusLine = hasFeed ? tr(STR_ATLAS_LAST_GOOD) : tr(STR_ATLAS_NO_DATA);
    clearWorkerSecrets();
    requestUpdate();
    return;
  }

  state = State::Fetching;
  workerDone.store(false, std::memory_order_release);
  workerCancel.store(false, std::memory_order_release);

  const BaseType_t created =
      xTaskCreatePinnedToCore(&workerTrampoline, "AtlasFetch", FETCH_WORKER_STACK, this, FETCH_WORKER_PRIORITY,
                              &workerTask, 0);
  if (created != pdPASS) {
    workerTask = nullptr;
    state = State::Ready;
    errorLine = tr(STR_ATLAS_MEMORY_ERROR);
    statusLine = hasFeed ? tr(STR_ATLAS_LAST_GOOD) : tr(STR_ATLAS_NO_DATA);
    clearWorkerSecrets();
    requestUpdate();
  }
}

void AtlasActivity::workerTrampoline(void* ctx) {
  static_cast<AtlasActivity*>(ctx)->runFetchWorker();
  vTaskDelete(nullptr);
}

void AtlasActivity::runFetchWorker() {
  HalPowerManager::Lock powerLock;
  FetchResult* const result = workerResult.get();
  atlas_feed::AtlasFeedJsonParser* const parser = result ? result->parser.get() : nullptr;
  if (!result || !parser) {
    workerDone.store(true, std::memory_order_release);
    return;
  }

  HttpDownloader::RequestOptions options;
  options.bearerToken = workerToken;
  options.cancelFlag = &workerCancel;
  options.timeoutMs = 8000;
  options.overallTimeoutMs = 12000;
  options.maxRedirects = 0;

  const bool ok = HttpDownloader::fetchUrl(
      workerUrl,
      [parser](const uint8_t* data, const size_t len) {
        return parser->feed(reinterpret_cast<const char*>(data), len);
      },
      options);

  if (workerCancel.load(std::memory_order_acquire)) {
    result->aborted = true;
  } else if (!ok) {
    result->bodyTooLarge = parser->getError() == atlas_feed::ParseError::BodyTooLarge;
    result->parseError = parser->getError();
  } else if (parser->getRawJsonSize() == 0U) {
    result->parseError = atlas_feed::ParseError::Syntax;
  } else if (parser->finish()) {
    result->ok = true;
  } else {
    result->parseError = parser->getError();
  }

  workerDone.store(true, std::memory_order_release);
}

void AtlasActivity::pollFetchWorker() {
  if (state != State::Fetching || !workerDone.load(std::memory_order_acquire)) return;
  workerTask = nullptr;
  state = State::Ready;
  applyFetchResult();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  clearWorkerSecrets();
  requestUpdate();
}

void AtlasActivity::applyFetchResult() {
  if (!workerResult || !workerResult->parser) {
    errorLine = tr(STR_ATLAS_MEMORY_ERROR);
    statusLine = hasFeed ? tr(STR_ATLAS_LAST_GOOD) : tr(STR_ATLAS_NO_DATA);
    return;
  }
  const FetchResult& result = *workerResult;
  const atlas_feed::AtlasFeedJsonParser& parser = *result.parser;

  if (result.aborted) {
    errorLine = tr(STR_ATLAS_FETCH_CANCELLED);
    statusLine = hasFeed ? tr(STR_ATLAS_LAST_GOOD) : tr(STR_ATLAS_NO_DATA);
    return;
  }

  if (!result.ok) {
    if (result.bodyTooLarge || result.parseError != atlas_feed::ParseError::None) {
      errorLine = std::string(tr(STR_ATLAS_PARSE_FAILED)) + ": " +
                  atlas_feed::AtlasFeedJsonParser::errorName(result.parseError);
    } else {
      errorLine = tr(STR_ATLAS_FETCH_FAILED);
    }
    statusLine = hasFeed ? tr(STR_ATLAS_LAST_GOOD) : tr(STR_ATLAS_NO_DATA);
    cacheIsStale = hasFeed;
    return;
  }

  copyFeed(feed, parser.getFeed());
  hasFeed = true;
  cacheIsStale = false;
  selectedTask = 0;

  if (!AtlasFeedCache::saveValidatedRaw(parser.getRawJson(), parser.getRawJsonSize())) {
    errorLine = tr(STR_ATLAS_CACHE_WRITE_FAILED);
  } else {
    errorLine.clear();
  }
  statusLine = std::string(tr(STR_ATLAS_UPDATED_PREFIX)) + feed.generatedAt;
}

void AtlasActivity::clearWorkerSecrets() {
  secureClear(workerToken);
  secureClear(workerUrl);
  workerResult.reset();
}

void AtlasActivity::loop() {
  pollFetchWorker();
  if (state == State::Fetching || state == State::WifiSelection) return;

  if (handleTouchActions()) return;

  const int taskCount = static_cast<int>(feed.taskCount);
  if (hasFeed && taskCount > 0) {
    buttonNavigator.onNext([this, taskCount] {
      selectedTask = ButtonNavigator::nextIndex(selectedTask, taskCount);
      requestUpdate();
    });
    buttonNavigator.onPrevious([this, taskCount] {
      selectedTask = ButtonNavigator::previousIndex(selectedTask, taskCount);
      requestUpdate();
    });

    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Up) {
      selectedTask = ButtonNavigator::nextIndex(selectedTask, taskCount);
      requestUpdate();
      return;
    }
    if (swipe == MappedInputManager::SwipeDir::Down) {
      selectedTask = ButtonNavigator::previousIndex(selectedTask, taskCount);
      requestUpdate();
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    beginRefresh();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome(HomeMenuItem::ATLAS);
  }
}

void AtlasActivity::buildTaskSubtitle(const atlas_feed::Task& task, char* out, const size_t outSize) const {
  char due[16] = {};
  if (strlen(task.due) >= 10U && task.due[4] == '-' && task.due[7] == '-') {
    snprintf(due, sizeof(due), "%.2s/%.2s", task.due + 8, task.due + 5);
  } else if (task.due[0] != '\0') {
    snprintf(due, sizeof(due), "%s", task.due);
  }

  const char* identity = task.identifier[0] != '\0' ? task.identifier : task.project;
  if (identity[0] != '\0' && due[0] != '\0') {
    snprintf(out, outSize, "P%u - %s - %s", static_cast<unsigned>(task.priority), identity, due);
  } else if (identity[0] != '\0') {
    snprintf(out, outSize, "P%u - %s", static_cast<unsigned>(task.priority), identity);
  } else if (due[0] != '\0') {
    snprintf(out, outSize, "P%u - %s", static_cast<unsigned>(task.priority), due);
  } else {
    snprintf(out, outSize, "P%u - %s", static_cast<unsigned>(task.priority), task.state);
  }
}

std::string AtlasActivity::buildAgentLine(const size_t index) const {
  if (index >= feed.agentCount) return {};
  const auto& agent = feed.agents[index];
  std::string line = agent.name;
  if (agent.summary[0] != '\0') {
    line += ": ";
    line += agent.summary;
  } else if (agent.state[0] != '\0') {
    line += " - ";
    line += agent.state;
  }
  return line;
}

bool AtlasActivity::handleTouchActions() {
  if (!mappedInput.hasTouch()) return false;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, false, false);
  const int actionTop = screen.y + screen.height - 44;
  const Rect backRect{screen.x, actionTop, screen.width / 2, 44};
  const Rect refreshRect{screen.x + screen.width / 2, actionTop, screen.width - screen.width / 2, 44};

  int x = 0;
  int y = 0;
  if (!mappedInput.wasScreenTapped(x, y)) return false;
  if (contains(backRect, x, y)) {
    onGoHome(HomeMenuItem::ATLAS);
    return true;
  }
  if (contains(refreshRect, x, y)) {
    beginRefresh();
    return true;
  }
  (void)metrics;
  return false;
}

void AtlasActivity::renderTouchActions(const Rect& screen, const ThemeMetrics& metrics) const {
  if (!mappedInput.hasTouch()) return;
  const int actionTop = screen.y + screen.height - 44;
  const int half = screen.width / 2;
  renderer.drawText(UI_10_FONT_ID, screen.x + metrics.contentSidePadding, actionTop + 12, tr(STR_BACK));
  const int refreshWidth = renderer.getTextWidth(UI_10_FONT_ID, tr(STR_ATLAS_REFRESH));
  renderer.drawText(UI_10_FONT_ID, screen.x + screen.width - metrics.contentSidePadding - refreshWidth, actionTop + 12,
                    tr(STR_ATLAS_REFRESH));
  renderer.drawLine(screen.x + half, actionTop + 6, screen.x + half, actionTop + 34);
}

void AtlasActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, !mappedInput.hasTouch(), false);
  const int pageWidth = screen.width;

  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight}, "Atlas");
  GUI.drawSubHeader(renderer,
                    Rect{screen.x, screen.y + metrics.topPadding + metrics.headerHeight, screen.width,
                         metrics.tabBarHeight},
                    statusLine.c_str(), cacheIsStale ? tr(STR_ATLAS_CACHED_BADGE) : nullptr);

  const int contentTop =
      screen.y + metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  const int footerReserve = mappedInput.hasTouch() ? 52 : metrics.verticalSpacing;
  const int agentReserve = hasFeed ? 68 : 0;
  const int contentBottom = screen.y + screen.height - footerReserve;
  const int contentHeight = std::max(0, contentBottom - contentTop);

  if (state == State::Fetching) {
    UITheme::drawCenteredWrappedText(renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, UI_12_FONT_ID,
                                     tr(STR_ATLAS_REFRESHING), 2, true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (!errorLine.empty()) {
    const int y = contentTop;
    renderer.drawText(UI_10_FONT_ID, screen.x + metrics.contentSidePadding, y, errorLine.c_str(), true,
                      EpdFontFamily::BOLD);
  }

  if (!hasFeed) {
    const Rect emptyBounds{screen.x + metrics.contentSidePadding, contentTop + 40,
                           screen.width - metrics.contentSidePadding * 2, contentHeight - 40};
    UITheme::drawCenteredWrappedText(renderer, emptyBounds, UI_10_FONT_ID, statusLine.c_str(), 4);
  } else if (feed.taskCount == 0) {
    renderer.drawText(UI_10_FONT_ID, screen.x + metrics.contentSidePadding, contentTop + 34, tr(STR_ATLAS_NO_TASKS), true,
                      EpdFontFamily::BOLD);
  } else {
    const int listTop = contentTop + (errorLine.empty() ? 0 : 32);
    const int listHeight = std::max(0, contentHeight - agentReserve - (errorLine.empty() ? 0 : 32));
    GUI.drawList(
        renderer, Rect{screen.x, listTop, screen.width, listHeight}, static_cast<int>(feed.taskCount), selectedTask,
        [this](int index) { return std::string(feed.tasks[index].title); },
        [this](int index) {
          char subtitle[96];
          buildTaskSubtitle(feed.tasks[index], subtitle, sizeof(subtitle));
          return std::string(subtitle);
        });
  }

  if (hasFeed) {
    int y = screen.y + screen.height - footerReserve - 58;
    if (!mappedInput.hasTouch()) y -= metrics.buttonHintsHeight;
    y = std::max(y, contentTop + 12);
    renderer.drawText(UI_10_FONT_ID, screen.x + metrics.contentSidePadding, y, tr(STR_ATLAS_AGENTS), true,
                      EpdFontFamily::BOLD);
    if (feed.agentCount == 0) {
      renderer.drawText(SMALL_FONT_ID, screen.x + metrics.contentSidePadding, y + 22, tr(STR_ATLAS_NO_AGENTS));
    } else {
      const size_t visibleAgents = std::min(feed.agentCount, static_cast<size_t>(2));
      for (size_t i = 0; i < visibleAgents; ++i) {
        const auto line = buildAgentLine(i);
        const auto clipped =
            renderer.truncatedText(SMALL_FONT_ID, line.c_str(), pageWidth - metrics.contentSidePadding * 2);
        renderer.drawText(SMALL_FONT_ID, screen.x + metrics.contentSidePadding, y + 22 + static_cast<int>(i) * 16,
                          clipped.c_str());
      }
    }
  }

  renderTouchActions(screen, metrics);
  if (!mappedInput.hasTouch()) {
    const bool canNavigate = hasFeed && feed.taskCount > 1;
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_ATLAS_REFRESH), canNavigate ? tr(STR_DIR_UP) : "",
                                              canNavigate ? tr(STR_DIR_DOWN) : "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
