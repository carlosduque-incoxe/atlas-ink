#include "HomeActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "AtlasDashboardFormat.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr int SUMMARY_TOP = 76;
constexpr int SUMMARY_HEIGHT = 134;
constexpr int TASK_CARD_GAP = 6;
constexpr int TASK_CARD_HEIGHT = 44;
constexpr int ACTION_ROW_HEIGHT = 38;
constexpr int ACTION_ROW_GAP = 5;
constexpr int ACTION_MARKER_WIDTH = 8;
constexpr int ACTION_TEXT_PAD_X = 18;
constexpr int ACTION_TEXT_PAD_Y = 10;
constexpr size_t HOME_CRITICAL_VISIBLE = 2;
constexpr const char* ATLAS_LEARNING_PATH = "/Atlas-Aprender";

void copyFeed(atlas_feed::Feed& dst, const atlas_feed::Feed& src) { memcpy(&dst, &src, sizeof(dst)); }

}  // namespace

HomeActivity::Action HomeActivity::actionFromIndex(const int index) {
  switch (index) {
    case 0:
      return Action::Tasks;
    case 1:
      return Action::Learn;
    case 2:
      return Action::Library;
    case 3:
      return Action::Transfer;
    case 4:
      return Action::Settings;
    default:
      return Action::Tasks;
  }
}

int HomeActivity::actionToIndex(const Action action) {
  switch (action) {
    case Action::Tasks:
      return 0;
    case Action::Learn:
      return 1;
    case Action::Library:
      return 2;
    case Action::Transfer:
      return 3;
    case Action::Settings:
      return 4;
    case Action::Count:
    default:
      return 0;
  }
}

const char* HomeActivity::actionLabel(const Action action) {
  switch (action) {
    case Action::Tasks:
      return tr(STR_ATLAS_HOME_TASKS);
    case Action::Learn:
      return tr(STR_ATLAS_HOME_LEARN);
    case Action::Library:
      return tr(STR_ATLAS_HOME_LIBRARY);
    case Action::Transfer:
      return tr(STR_ATLAS_HOME_TRANSFER);
    case Action::Settings:
      return tr(STR_ATLAS_HOME_SETTINGS);
    case Action::Count:
    default:
      return "";
  }
}

HomeActivity::Action HomeActivity::menuItemToAction(const HomeMenuItem item) {
  switch (item) {
    case HomeMenuItem::FILE_BROWSER:
      return Action::Library;
    case HomeMenuItem::FILE_TRANSFER:
      return Action::Transfer;
    case HomeMenuItem::SETTINGS_MENU:
      return Action::Settings;
    case HomeMenuItem::ATLAS:
    case HomeMenuItem::NONE:
    case HomeMenuItem::RECENTS:
    case HomeMenuItem::OPDS_BROWSER:
    default:
      return Action::Tasks;
  }
}

void HomeActivity::loadCachedFeed() {
  auto parser = makeUniqueNoThrow<atlas_feed::AtlasFeedJsonParser>();
  if (!parser) {
    hasFeed = false;
    return;
  }

  if (AtlasFeedCache::load(*parser)) {
    copyFeed(feed, parser->getFeed());
    hasFeed = true;
  } else {
    memset(&feed, 0, sizeof(feed));
    hasFeed = false;
  }
}

void HomeActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  loadCachedFeed();
  selectorIndex = actionToIndex(menuItemToAction(initialMenuItem));
  requestUpdate();
}

void HomeActivity::activateSelection() {
  switch (actionFromIndex(selectorIndex)) {
    case Action::Tasks:
      activityManager.goToAtlas();
      break;
    case Action::Learn:
      activityManager.goToFileBrowser(ATLAS_LEARNING_PATH);
      break;
    case Action::Library:
      activityManager.goToFileBrowser();
      break;
    case Action::Transfer:
      activityManager.goToFileTransfer();
      break;
    case Action::Settings:
      activityManager.goToSettings();
      break;
    case Action::Count:
    default:
      break;
  }
}

bool HomeActivity::handleTouchActions(const Rect& screen, const ThemeMetrics& metrics) {
  const int menuTop = screen.y + SUMMARY_TOP + SUMMARY_HEIGHT + metrics.verticalSpacing;
  const int rowStep = ACTION_ROW_HEIGHT + ACTION_ROW_GAP;
  int row = -1;
  const auto touch = mappedInput.rowTouch(row, menuTop, rowStep, actionCount(), screen.x + metrics.contentSidePadding,
                                          screen.x + screen.width - metrics.contentSidePadding, ACTION_ROW_HEIGHT);
  if (touch == MappedInputManager::RowTouch::None) return false;

  selectorIndex = row;
  if (touch == MappedInputManager::RowTouch::Tap) {
    activateSelection();
  } else {
    requestUpdate();
  }
  return true;
}

void HomeActivity::loop() {
  buttonNavigator.onNext([this] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, actionCount());
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, actionCount());
    requestUpdate();
  });

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, actionCount());
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, actionCount());
    requestUpdate();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, !mappedInput.hasTouch(), false);
  if (handleTouchActions(screen, metrics)) return;

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) backPressSeen = true;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && backPressSeen) {
    backPressSeen = false;
    if (selectorIndex != actionToIndex(Action::Tasks)) {
      selectorIndex = actionToIndex(Action::Tasks);
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
  }
}

void HomeActivity::renderHeader(const Rect& screen, const ThemeMetrics& metrics) const {
  const int left = screen.x + metrics.contentSidePadding;
  const int right = screen.x + screen.width - metrics.contentSidePadding;
  const int titleY = screen.y + 12;
  renderer.drawText(UI_12_FONT_ID, left, titleY, tr(STR_ATLAS_HOME_TITLE), true, EpdFontFamily::BOLD);

  char generatedAt[atlas_dashboard::GENERATED_AT_DISPLAY_SIZE] = {};
  char freshness[64] = {};
  if (hasFeed && feed.generatedAt[0] != '\0') {
    atlas_dashboard::formatGeneratedAt(feed.generatedAt, generatedAt, sizeof(generatedAt));
    snprintf(freshness, sizeof(freshness), "%s %s", tr(STR_ATLAS_LAST_UPDATE), generatedAt);
  } else {
    snprintf(freshness, sizeof(freshness), "%s %s", tr(STR_ATLAS_LAST_UPDATE), tr(STR_ATLAS_NO_DATA));
  }
  renderer.drawText(SMALL_FONT_ID, left, titleY + 30, freshness);
  if (hasFeed) {
    const char* badge = tr(STR_ATLAS_CACHED_OFFLINE);
    const int badgeWidth = renderer.getTextWidth(SMALL_FONT_ID, badge, EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, right - badgeWidth, titleY + 30, badge, true, EpdFontFamily::BOLD);
  }
}

void HomeActivity::renderSummary(const Rect& screen, const ThemeMetrics& metrics) const {
  const int left = screen.x + metrics.contentSidePadding;
  const int width = screen.width - metrics.contentSidePadding * 2;
  const int top = screen.y + SUMMARY_TOP;

  char countLine[64] = {};
  const size_t criticalCount = hasFeed ? atlas_dashboard::countCriticalTasks(feed) : 0U;
  snprintf(countLine, sizeof(countLine), tr(STR_ATLAS_CRITICAL_COUNT_FMT), static_cast<unsigned>(criticalCount));
  renderer.drawText(UI_12_FONT_ID, left, top, countLine, true, EpdFontFamily::BOLD);

  if (!hasFeed) {
    UITheme::drawCenteredWrappedText(renderer, Rect{left, top + 36, width, 86}, UI_10_FONT_ID,
                                     tr(STR_ATLAS_HOME_NO_CACHE), 3);
    return;
  }

  if (criticalCount == 0U) {
    renderer.drawText(UI_10_FONT_ID, left, top + 42, tr(STR_ATLAS_NO_TASKS));
    return;
  }

  size_t indexes[HOME_CRITICAL_VISIBLE] = {};
  const size_t visible = atlas_dashboard::topCriticalTaskIndexes(feed, indexes, HOME_CRITICAL_VISIBLE);
  for (size_t i = 0; i < visible; ++i) {
    const atlas_feed::Task& task = feed.tasks[indexes[i]];
    const int cardY = top + 38 + static_cast<int>(i) * (TASK_CARD_HEIGHT + TASK_CARD_GAP);
    renderer.fillRect(left, cardY, width, TASK_CARD_HEIGHT);
    char label[32] = {};
    snprintf(label, sizeof(label), tr(STR_ATLAS_PRIORITY_FMT), static_cast<unsigned>(task.priority));
    renderer.drawText(SMALL_FONT_ID, left + 10, cardY + 8, label, false, EpdFontFamily::BOLD);
    const int titleX = left + 58;
    const int titleWidth = width - 70;
    const auto title = renderer.truncatedText(UI_10_FONT_ID, task.title, titleWidth, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, titleX, cardY + 11, title.c_str(), false, EpdFontFamily::BOLD);
  }
}

void HomeActivity::renderActions(const Rect& screen, const ThemeMetrics& metrics) const {
  const int rowX = screen.x + metrics.contentSidePadding;
  const int rowW = screen.width - metrics.contentSidePadding * 2;
  const int menuTop = screen.y + SUMMARY_TOP + SUMMARY_HEIGHT + metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);

  for (int i = 0; i < actionCount(); ++i) {
    const int rowY = menuTop + i * (ACTION_ROW_HEIGHT + ACTION_ROW_GAP);
    const bool selected = i == selectorIndex;
    if (selected) {
      renderer.fillRect(rowX, rowY, rowW, ACTION_ROW_HEIGHT);
      renderer.fillRect(rowX + 3, rowY + 4, ACTION_MARKER_WIDTH, ACTION_ROW_HEIGHT - 8, false);
    } else {
      renderer.drawRect(rowX, rowY, rowW, ACTION_ROW_HEIGHT);
      renderer.fillRect(rowX, rowY, ACTION_MARKER_WIDTH, ACTION_ROW_HEIGHT);
    }

    const char* label = actionLabel(actionFromIndex(i));
    const int textY =
        rowY + ACTION_TEXT_PAD_Y + std::max(0, (ACTION_ROW_HEIGHT - ACTION_TEXT_PAD_Y * 2 - lineHeight) / 2);
    renderer.drawText(UI_12_FONT_ID, rowX + ACTION_TEXT_PAD_X, textY, label, !selected, EpdFontFamily::BOLD);
  }
}

void HomeActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, !mappedInput.hasTouch(), false);

  renderHeader(screen, metrics);
  renderSummary(screen, metrics);
  renderActions(screen, metrics);

  if (!mappedInput.hasTouch()) {
    const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
