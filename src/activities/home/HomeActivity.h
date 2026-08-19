#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "atlas/AtlasFeedCache.h"
#include "util/ButtonNavigator.h"

struct Rect;
struct ThemeMetrics;

class HomeActivity final : public Activity {
  enum class Action : uint8_t { Tasks, Learn, Library, Transfer, Settings, Count };

  ButtonNavigator buttonNavigator;
  atlas_feed::Feed feed{};
  bool hasFeed = false;
  // Home can be entered while Back is still held. Ignore that stale release
  // until this Home instance observes a fresh Back press.
  bool backPressSeen = false;
  int selectorIndex = 0;
  const HomeMenuItem initialMenuItem;

  static constexpr int actionCount() { return static_cast<int>(Action::Count); }
  static Action actionFromIndex(int index);
  static int actionToIndex(Action action);
  static const char* actionLabel(Action action);
  static Action menuItemToAction(HomeMenuItem item);

  void loadCachedFeed();
  void activateSelection();
  void renderHeader(const Rect& screen, const ThemeMetrics& metrics) const;
  void renderSummary(const Rect& screen, const ThemeMetrics& metrics) const;
  void renderActions(const Rect& screen, const ThemeMetrics& metrics) const;
  bool handleTouchActions(const Rect& screen, const ThemeMetrics& metrics);

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        HomeMenuItem initialMenuItemValue = HomeMenuItem::NONE)
      : Activity("Home", renderer, mappedInput), initialMenuItem(initialMenuItemValue) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }
};
