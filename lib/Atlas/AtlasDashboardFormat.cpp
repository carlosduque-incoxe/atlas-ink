#include "AtlasDashboardFormat.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace atlas_dashboard {
namespace {

bool isDigitAt(const char* text, const size_t index) {
  return std::isdigit(static_cast<unsigned char>(text[index])) != 0;
}

bool isCanonicalGeneratedAt(const char* text) {
  if (!text || std::strlen(text) < 16U) return false;
  return isDigitAt(text, 0) && isDigitAt(text, 1) && isDigitAt(text, 2) && isDigitAt(text, 3) && text[4] == '-' &&
         isDigitAt(text, 5) && isDigitAt(text, 6) && text[7] == '-' && isDigitAt(text, 8) && isDigitAt(text, 9) &&
         (text[10] == 'T' || text[10] == ' ') && isDigitAt(text, 11) && isDigitAt(text, 12) && text[13] == ':' &&
         isDigitAt(text, 14) && isDigitAt(text, 15);
}

bool criticalTaskIsMoreImportant(const atlas_feed::Task& candidate, const atlas_feed::Task& current) {
  return candidate.priority > current.priority;
}

}  // namespace

bool isCriticalPriority(const uint8_t priority) { return priority >= CRITICAL_PRIORITY; }

void copyTerminated(char* out, const size_t outSize, const char* in) {
  if (!out || outSize == 0U) return;
  if (!in) {
    out[0] = '\0';
    return;
  }
  std::snprintf(out, outSize, "%s", in);
  out[outSize - 1U] = '\0';
}

bool formatGeneratedAt(const char* generatedAt, char* out, const size_t outSize) {
  if (!out || outSize == 0U) return false;
  out[0] = '\0';

  if (!isCanonicalGeneratedAt(generatedAt)) {
    copyTerminated(out, outSize, generatedAt ? generatedAt : "");
    return false;
  }

  std::snprintf(out, outSize, "%c%c/%c%c %c%c:%c%c", generatedAt[8], generatedAt[9], generatedAt[5], generatedAt[6],
                generatedAt[11], generatedAt[12], generatedAt[14], generatedAt[15]);
  out[outSize - 1U] = '\0';
  return outSize >= GENERATED_AT_DISPLAY_SIZE;
}

TaskPageRange taskPageRange(const size_t taskCount, size_t selectedIndex, const size_t cardsPerPage) {
  TaskPageRange range{};
  if (taskCount == 0U || cardsPerPage == 0U) return range;

  if (selectedIndex >= taskCount) selectedIndex = taskCount - 1U;
  range.totalPages = (taskCount + cardsPerPage - 1U) / cardsPerPage;
  range.pageIndex = selectedIndex / cardsPerPage;
  range.start = range.pageIndex * cardsPerPage;
  range.count = std::min(cardsPerPage, taskCount - range.start);
  return range;
}

size_t countCriticalTasks(const atlas_feed::Feed& feed) {
  size_t count = 0;
  for (size_t i = 0; i < feed.taskCount; ++i) {
    if (isCriticalPriority(feed.tasks[i].priority)) ++count;
  }
  return count;
}

size_t topCriticalTaskIndexes(const atlas_feed::Feed& feed, size_t* outIndexes, const size_t maxOut) {
  if (!outIndexes || maxOut == 0U) return 0U;

  size_t outCount = 0;
  for (size_t i = 0; i < feed.taskCount; ++i) {
    if (!isCriticalPriority(feed.tasks[i].priority)) continue;

    size_t insertAt = outCount;
    while (insertAt > 0U && criticalTaskIsMoreImportant(feed.tasks[i], feed.tasks[outIndexes[insertAt - 1U]])) {
      --insertAt;
    }

    if (insertAt >= maxOut) continue;
    const size_t last = std::min(outCount, maxOut - 1U);
    for (size_t j = last; j > insertAt; --j) {
      outIndexes[j] = outIndexes[j - 1U];
    }
    outIndexes[insertAt] = i;
    if (outCount < maxOut) ++outCount;
  }
  return outCount;
}

size_t sleepTaskIndex(const atlas_feed::Feed& feed) {
  if (feed.taskCount == 0U) return NO_TASK_INDEX;

  size_t best = 0;
  for (size_t i = 1; i < feed.taskCount; ++i) {
    if (feed.tasks[i].priority > feed.tasks[best].priority) best = i;
  }
  return best;
}

}  // namespace atlas_dashboard
