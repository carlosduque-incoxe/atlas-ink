#pragma once

#include <cstddef>
#include <cstdint>

#include "../JsonParser/AtlasFeedJsonParser.h"

namespace atlas_dashboard {

static constexpr uint8_t CRITICAL_PRIORITY = 4;
static constexpr size_t TASK_CARDS_PER_PAGE = 3;
static constexpr size_t GENERATED_AT_DISPLAY_SIZE = 12;  // "DD/MM HH:MM" + NUL
static constexpr size_t NO_TASK_INDEX = static_cast<size_t>(-1);

struct TaskPageRange {
  size_t start = 0;
  size_t count = 0;
  size_t pageIndex = 0;
  size_t totalPages = 0;
};

bool isCriticalPriority(uint8_t priority);
void copyTerminated(char* out, size_t outSize, const char* in);
bool formatGeneratedAt(const char* generatedAt, char* out, size_t outSize);
TaskPageRange taskPageRange(size_t taskCount, size_t selectedIndex, size_t cardsPerPage = TASK_CARDS_PER_PAGE);
size_t countCriticalTasks(const atlas_feed::Feed& feed);
size_t topCriticalTaskIndexes(const atlas_feed::Feed& feed, size_t* outIndexes, size_t maxOut);
size_t sleepTaskIndex(const atlas_feed::Feed& feed);

}  // namespace atlas_dashboard
