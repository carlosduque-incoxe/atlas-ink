#pragma once

#include <cstddef>
#include <cstdint>

namespace atlas_feed {

static constexpr size_t MAX_BODY_BYTES = 12 * 1024;
static constexpr size_t MAX_TASKS = 5;
static constexpr size_t MAX_AGENTS = 5;

static constexpr size_t MAX_GENERATED_AT_LEN = 32;
static constexpr size_t MAX_ETAG_LEN = 80;
static constexpr size_t MAX_TASK_ID_LEN = 32;
static constexpr size_t MAX_TASK_IDENTIFIER_LEN = 32;
// Aerovía caps titles at 160 Unicode code points. UTF-8 uses at most four
// bytes per code point, so the fixed buffer must cover the same contract.
static constexpr size_t MAX_TASK_TITLE_LEN = 160 * 4;
static constexpr size_t MAX_TASK_PROJECT_LEN = 48;
static constexpr size_t MAX_TASK_DUE_LEN = 32;
static constexpr size_t MAX_TASK_STATE_LEN = 16;
static constexpr size_t MAX_AGENT_NAME_LEN = 48;
static constexpr size_t MAX_AGENT_STATE_LEN = 16;
static constexpr size_t MAX_AGENT_SUMMARY_LEN = 128;

enum class ParseError : uint8_t {
  None,
  BodyTooLarge,
  Syntax,
  RootNotObject,
  DuplicateField,
  MissingField,
  UnsupportedSchema,
  InvalidType,
  LimitExceeded,
  FieldTooLong,
  InvalidUtf8,
  InvalidNumber,
  NestingTooDeep,
  TrailingData,
};

struct Task {
  char id[MAX_TASK_ID_LEN + 1];
  char identifier[MAX_TASK_IDENTIFIER_LEN + 1];
  char title[MAX_TASK_TITLE_LEN + 1];
  char project[MAX_TASK_PROJECT_LEN + 1];
  uint32_t projectId;
  uint8_t priority;
  char due[MAX_TASK_DUE_LEN + 1];
  char state[MAX_TASK_STATE_LEN + 1];
  bool hasProjectId;
};

struct Agent {
  char name[MAX_AGENT_NAME_LEN + 1];
  char state[MAX_AGENT_STATE_LEN + 1];
  char summary[MAX_AGENT_SUMMARY_LEN + 1];
};

struct Feed {
  char generatedAt[MAX_GENERATED_AT_LEN + 1];
  char etag[MAX_ETAG_LEN + 1];
  Task tasks[MAX_TASKS];
  Agent agents[MAX_AGENTS];
  size_t taskCount;
  size_t agentCount;
};

class AtlasFeedJsonParser {
 public:
  AtlasFeedJsonParser();

  void reset();
  bool feed(const char* data, size_t len);
  bool finish();

  const Feed& getFeed() const { return parsedFeed; }
  ParseError getError() const { return error; }
  const char* getRawJson() const { return raw; }
  size_t getRawJsonSize() const { return rawLen; }

  static const char* errorName(ParseError value);

 private:
  char raw[MAX_BODY_BYTES + 1];
  size_t rawLen;
  Feed parsedFeed;
  ParseError error;
};

bool parseFeedJson(const char* data, size_t len, Feed& feed, ParseError& error);

}  // namespace atlas_feed
