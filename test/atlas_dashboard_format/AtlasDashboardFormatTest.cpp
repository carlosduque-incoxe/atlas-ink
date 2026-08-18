#include <gtest/gtest.h>

#include <cstring>

#include "AtlasDashboardFormat.h"

namespace {

atlas_feed::Task task(const uint8_t priority, const char* title) {
  atlas_feed::Task value{};
  value.priority = priority;
  atlas_dashboard::copyTerminated(value.title, sizeof(value.title), title);
  return value;
}

atlas_feed::Feed feedWithPriorities(std::initializer_list<uint8_t> priorities) {
  atlas_feed::Feed feed{};
  size_t i = 0;
  for (const uint8_t priority : priorities) {
    feed.tasks[i] = task(priority, "Task");
    ++i;
  }
  feed.taskCount = i;
  return feed;
}

}  // namespace

TEST(AtlasDashboardFormat, FormatsCanonicalGeneratedAt) {
  char out[atlas_dashboard::GENERATED_AT_DISPLAY_SIZE] = {};
  EXPECT_TRUE(atlas_dashboard::formatGeneratedAt("2026-08-18T20:00:00Z", out, sizeof(out)));
  EXPECT_STREQ(out, "18/08 20:00");
}

TEST(AtlasDashboardFormat, MalformedGeneratedAtFallsBackToTerminatedInput) {
  char out[5] = {'x', 'x', 'x', 'x', 'x'};
  EXPECT_FALSE(atlas_dashboard::formatGeneratedAt("bad timestamp", out, sizeof(out)));
  EXPECT_EQ(out[sizeof(out) - 1U], '\0');
  EXPECT_STREQ(out, "bad ");
}

TEST(AtlasDashboardFormat, FixedBuffersAlwaysTerminate) {
  char single[1] = {'x'};
  atlas_dashboard::copyTerminated(single, sizeof(single), "abc");
  EXPECT_EQ(single[0], '\0');

  char shortDate[4] = {'x', 'x', 'x', 'x'};
  EXPECT_FALSE(atlas_dashboard::formatGeneratedAt("2026-08-18T20:00:00Z", shortDate, sizeof(shortDate)));
  EXPECT_EQ(shortDate[sizeof(shortDate) - 1U], '\0');
}

TEST(AtlasDashboardFormat, PageRangesCoverZeroToFiveTasks) {
  auto range = atlas_dashboard::taskPageRange(0, 0);
  EXPECT_EQ(range.start, 0U);
  EXPECT_EQ(range.count, 0U);
  EXPECT_EQ(range.totalPages, 0U);

  range = atlas_dashboard::taskPageRange(1, 0);
  EXPECT_EQ(range.start, 0U);
  EXPECT_EQ(range.count, 1U);
  EXPECT_EQ(range.pageIndex, 0U);
  EXPECT_EQ(range.totalPages, 1U);

  range = atlas_dashboard::taskPageRange(3, 2);
  EXPECT_EQ(range.start, 0U);
  EXPECT_EQ(range.count, 3U);
  EXPECT_EQ(range.totalPages, 1U);

  range = atlas_dashboard::taskPageRange(4, 3);
  EXPECT_EQ(range.start, 3U);
  EXPECT_EQ(range.count, 1U);
  EXPECT_EQ(range.pageIndex, 1U);
  EXPECT_EQ(range.totalPages, 2U);

  range = atlas_dashboard::taskPageRange(5, 99);
  EXPECT_EQ(range.start, 3U);
  EXPECT_EQ(range.count, 2U);
  EXPECT_EQ(range.pageIndex, 1U);
  EXPECT_EQ(range.totalPages, 2U);

  range = atlas_dashboard::taskPageRange(5, 2, 2);
  EXPECT_EQ(range.start, 2U);
  EXPECT_EQ(range.count, 2U);
  EXPECT_EQ(range.pageIndex, 1U);
  EXPECT_EQ(range.totalPages, 3U);

  range = atlas_dashboard::taskPageRange(5, 4, 2);
  EXPECT_EQ(range.start, 4U);
  EXPECT_EQ(range.count, 1U);
  EXPECT_EQ(range.pageIndex, 2U);
  EXPECT_EQ(range.totalPages, 3U);
}

TEST(AtlasDashboardFormat, PriorityThresholdIsExactlyFour) {
  EXPECT_FALSE(atlas_dashboard::isCriticalPriority(3));
  EXPECT_TRUE(atlas_dashboard::isCriticalPriority(4));
  EXPECT_TRUE(atlas_dashboard::isCriticalPriority(5));
}

TEST(AtlasDashboardFormat, SleepTaskSelectionIsStableByPriorityThenFeedOrder) {
  atlas_feed::Feed feed = feedWithPriorities({2, 4, 5, 5, 3});
  EXPECT_EQ(atlas_dashboard::sleepTaskIndex(feed), 2U);

  feed = feedWithPriorities({4, 4, 3});
  EXPECT_EQ(atlas_dashboard::sleepTaskIndex(feed), 0U);

  feed.taskCount = 0;
  EXPECT_EQ(atlas_dashboard::sleepTaskIndex(feed), atlas_dashboard::NO_TASK_INDEX);
}

TEST(AtlasDashboardFormat, TopCriticalTasksAreSortedAndStable) {
  atlas_feed::Feed feed = feedWithPriorities({4, 5, 3, 5, 4});
  size_t indexes[2] = {atlas_dashboard::NO_TASK_INDEX, atlas_dashboard::NO_TASK_INDEX};

  EXPECT_EQ(atlas_dashboard::countCriticalTasks(feed), 4U);
  EXPECT_EQ(atlas_dashboard::topCriticalTaskIndexes(feed, indexes, 2), 2U);
  EXPECT_EQ(indexes[0], 1U);
  EXPECT_EQ(indexes[1], 3U);
}
