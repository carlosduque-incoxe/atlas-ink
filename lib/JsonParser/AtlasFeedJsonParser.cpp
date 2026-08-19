#include "AtlasFeedJsonParser.h"

#include <cstring>

namespace atlas_feed {
namespace {

enum RootField : uint8_t {
  ROOT_SCHEMA = 1U << 0U,
  ROOT_GENERATED_AT = 1U << 1U,
  ROOT_ETAG = 1U << 2U,
  ROOT_TASKS = 1U << 3U,
  ROOT_AGENTS = 1U << 4U,
};

enum TaskField : uint16_t {
  TASK_ID = 1U << 0U,
  TASK_IDENTIFIER = 1U << 1U,
  TASK_TITLE = 1U << 2U,
  TASK_PROJECT_ID = 1U << 3U,
  TASK_PROJECT = 1U << 4U,
  TASK_PRIORITY = 1U << 5U,
  TASK_DUE = 1U << 6U,
  TASK_STATE = 1U << 7U,
};

enum AgentField : uint8_t {
  AGENT_NAME = 1U << 0U,
  AGENT_STATE = 1U << 1U,
  AGENT_SUMMARY = 1U << 2U,
};

constexpr size_t MAX_UNKNOWN_NESTING_DEPTH = 16;
constexpr size_t GENERATED_AT_UTC_LEN = 20;

bool isWs(const char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

bool isDigit(const char c) { return c >= '0' && c <= '9'; }

uint8_t parseTwoDigits(const char* value) { return static_cast<uint8_t>((value[0] - '0') * 10 + (value[1] - '0')); }

uint16_t parseFourDigits(const char* value) {
  return static_cast<uint16_t>((value[0] - '0') * 1000 + (value[1] - '0') * 100 + (value[2] - '0') * 10 +
                               (value[3] - '0'));
}

bool isLeapYear(const uint16_t year) { return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U; }

uint8_t daysInMonth(const uint16_t year, const uint8_t month) {
  static constexpr uint8_t DAYS_BY_MONTH[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2U && isLeapYear(year)) return 29;
  return DAYS_BY_MONTH[month - 1U];
}

bool validGeneratedAtUtc(const char* value) {
  if (strlen(value) != GENERATED_AT_UTC_LEN) return false;
  if (value[4] != '-' || value[7] != '-' || value[10] != 'T' || value[13] != ':' || value[16] != ':' ||
      value[19] != 'Z') {
    return false;
  }

  for (uint8_t i = 0; i < GENERATED_AT_UTC_LEN; ++i) {
    if (i == 4U || i == 7U || i == 10U || i == 13U || i == 16U || i == 19U) continue;
    if (!isDigit(value[i])) return false;
  }

  const uint16_t year = parseFourDigits(value);
  if (year == 0U) return false;

  const uint8_t month = parseTwoDigits(value + 5);
  if (month < 1U || month > 12U) return false;

  const uint8_t day = parseTwoDigits(value + 8);
  if (day < 1U || day > daysInMonth(year, month)) return false;

  const uint8_t hour = parseTwoDigits(value + 11);
  if (hour > 23U) return false;

  const uint8_t minute = parseTwoDigits(value + 14);
  if (minute > 59U) return false;

  const uint8_t second = parseTwoDigits(value + 17);
  return second <= 59U;
}

bool hexValue(const char c, uint16_t& value) {
  if (c >= '0' && c <= '9') {
    value = static_cast<uint16_t>(c - '0');
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    value = static_cast<uint16_t>(10 + c - 'a');
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    value = static_cast<uint16_t>(10 + c - 'A');
    return true;
  }
  return false;
}

bool appendUtf8(char* out, const size_t outSize, size_t& outLen, const uint32_t cp) {
  if (cp == 0 || cp > 0x10FFFFU || (cp >= 0xD800U && cp <= 0xDFFFU)) return false;

  char bytes[4];
  size_t count = 0;
  if (cp <= 0x7FU) {
    bytes[count++] = static_cast<char>(cp);
  } else if (cp <= 0x7FFU) {
    bytes[count++] = static_cast<char>(0xC0U | (cp >> 6U));
    bytes[count++] = static_cast<char>(0x80U | (cp & 0x3FU));
  } else if (cp <= 0xFFFFU) {
    bytes[count++] = static_cast<char>(0xE0U | (cp >> 12U));
    bytes[count++] = static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU));
    bytes[count++] = static_cast<char>(0x80U | (cp & 0x3FU));
  } else {
    bytes[count++] = static_cast<char>(0xF0U | (cp >> 18U));
    bytes[count++] = static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU));
    bytes[count++] = static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU));
    bytes[count++] = static_cast<char>(0x80U | (cp & 0x3FU));
  }

  if (outLen + count >= outSize) return false;
  memcpy(out + outLen, bytes, count);
  outLen += count;
  return true;
}

bool validUtf8(const char* value) {
  const auto* s = reinterpret_cast<const uint8_t*>(value);
  while (*s) {
    if (*s <= 0x7FU) {
      ++s;
      continue;
    }

    uint32_t cp = 0;
    size_t continuation = 0;
    if ((*s & 0xE0U) == 0xC0U) {
      cp = *s & 0x1FU;
      continuation = 1;
      if (cp < 2U) return false;  // overlong
    } else if ((*s & 0xF0U) == 0xE0U) {
      cp = *s & 0x0FU;
      continuation = 2;
    } else if ((*s & 0xF8U) == 0xF0U) {
      cp = *s & 0x07U;
      continuation = 3;
    } else {
      return false;
    }
    ++s;

    for (size_t i = 0; i < continuation; ++i) {
      if ((*s & 0xC0U) != 0x80U) return false;
      cp = (cp << 6U) | (*s & 0x3FU);
      ++s;
    }

    if ((continuation == 2 && cp < 0x800U) || (continuation == 3 && cp < 0x10000U)) return false;
    if (cp > 0x10FFFFU || (cp >= 0xD800U && cp <= 0xDFFFU)) return false;
  }
  return true;
}

class JsonReader {
 public:
  JsonReader(const char* data, const size_t len, Feed& feedOut) : cursor(data), end(data + len), feed(feedOut) {}

  bool parse() {
    clearFeed(feed);
    skipWs();
    if (cursor == end) return fail(ParseError::Syntax);
    if (*cursor != '{') return fail(ParseError::RootNotObject);
    if (!parseRootObject()) return false;
    skipWs();
    if (cursor != end) return fail(ParseError::TrailingData);
    if ((rootSeen & ROOT_SCHEMA) == 0 || (rootSeen & ROOT_GENERATED_AT) == 0 || (rootSeen & ROOT_TASKS) == 0 ||
        (rootSeen & ROOT_AGENTS) == 0) {
      return fail(ParseError::MissingField);
    }
    return true;
  }

  ParseError getError() const { return error; }

 private:
  enum class ValueKind : uint8_t {
    String,
    Number,
    Object,
    Array,
    Bool,
    Null,
  };

  static void clearFeed(Feed& value) {
    value.generatedAt[0] = '\0';
    value.etag[0] = '\0';
    value.taskCount = 0;
    value.agentCount = 0;
    for (Task& task : value.tasks) {
      task.id[0] = '\0';
      task.identifier[0] = '\0';
      task.title[0] = '\0';
      task.project[0] = '\0';
      task.projectId = 0;
      task.priority = 0;
      task.due[0] = '\0';
      task.state[0] = '\0';
      task.hasProjectId = false;
    }
    for (Agent& agent : value.agents) {
      agent.name[0] = '\0';
      agent.state[0] = '\0';
      agent.summary[0] = '\0';
    }
  }

  bool fail(const ParseError value) {
    if (error == ParseError::None) error = value;
    return false;
  }

  void skipWs() {
    while (cursor < end && isWs(*cursor)) ++cursor;
  }

  bool consume(const char expected) {
    skipWs();
    if (cursor == end || *cursor != expected) return fail(ParseError::Syntax);
    ++cursor;
    return true;
  }

  bool parseRootObject() {
    if (!consume('{')) return false;
    skipWs();
    if (cursor < end && *cursor == '}') {
      ++cursor;
      return true;
    }

    while (true) {
      char key[32];
      bool keyTruncated = false;
      if (!parseString(key, sizeof(key), keyTruncated, false)) return false;
      if (!consume(':')) return false;

      if (!keyTruncated && strcmp(key, "schema") == 0) {
        if (!markRoot(ROOT_SCHEMA)) return false;
        uint32_t schema = 0;
        if (!parseUnsignedInteger(schema)) return false;
        if (schema != 1) return fail(ParseError::UnsupportedSchema);
      } else if (!keyTruncated && strcmp(key, "generated_at") == 0) {
        if (!markRoot(ROOT_GENERATED_AT)) return false;
        if (!parseStringField(feed.generatedAt, sizeof(feed.generatedAt))) return false;
        if (!validGeneratedAtUtc(feed.generatedAt)) return fail(ParseError::InvalidTimestamp);
      } else if (!keyTruncated && strcmp(key, "etag") == 0) {
        if (!markRoot(ROOT_ETAG)) return false;
        if (!parseStringField(feed.etag, sizeof(feed.etag))) return false;
      } else if (!keyTruncated && strcmp(key, "tasks") == 0) {
        if (!markRoot(ROOT_TASKS)) return false;
        if (!parseTasksArray()) return false;
      } else if (!keyTruncated && strcmp(key, "agents") == 0) {
        if (!markRoot(ROOT_AGENTS)) return false;
        if (!parseAgentsArray()) return false;
      } else if (!skipValue()) {
        return false;
      }

      skipWs();
      if (cursor < end && *cursor == ',') {
        ++cursor;
        skipWs();
        if (cursor < end && *cursor == '}') return fail(ParseError::Syntax);
        continue;
      }
      if (cursor < end && *cursor == '}') {
        ++cursor;
        return true;
      }
      return fail(ParseError::Syntax);
    }
  }

  bool markRoot(const uint8_t field) {
    if ((rootSeen & field) != 0) return fail(ParseError::DuplicateField);
    rootSeen |= field;
    return true;
  }

  bool markTask(uint16_t& seen, const uint16_t field) {
    if ((seen & field) != 0) return fail(ParseError::DuplicateField);
    seen |= field;
    return true;
  }

  bool markAgent(uint8_t& seen, const uint8_t field) {
    if ((seen & field) != 0) return fail(ParseError::DuplicateField);
    seen |= field;
    return true;
  }

  bool parseTasksArray() {
    if (!consume('[')) return false;
    skipWs();
    if (cursor < end && *cursor == ']') {
      ++cursor;
      return true;
    }

    while (true) {
      if (feed.taskCount >= MAX_TASKS) return fail(ParseError::LimitExceeded);
      Task& task = feed.tasks[feed.taskCount];
      if (!parseTaskObject(task)) return false;
      ++feed.taskCount;

      skipWs();
      if (cursor < end && *cursor == ',') {
        ++cursor;
        skipWs();
        if (cursor < end && *cursor == ']') return fail(ParseError::Syntax);
        continue;
      }
      if (cursor < end && *cursor == ']') {
        ++cursor;
        return true;
      }
      return fail(ParseError::Syntax);
    }
  }

  bool parseTaskObject(Task& task) {
    if (!consume('{')) return false;
    uint16_t seen = 0;
    skipWs();
    if (cursor < end && *cursor == '}') {
      ++cursor;
      return fail(ParseError::MissingField);
    }

    while (true) {
      char key[32];
      bool keyTruncated = false;
      if (!parseString(key, sizeof(key), keyTruncated, false)) return false;
      if (!consume(':')) return false;

      if (!keyTruncated && strcmp(key, "id") == 0) {
        if (!markTask(seen, TASK_ID)) return false;
        if (!parseIdField(task.id, sizeof(task.id))) return false;
      } else if (!keyTruncated && strcmp(key, "identifier") == 0) {
        if (!markTask(seen, TASK_IDENTIFIER)) return false;
        if (!parseStringField(task.identifier, sizeof(task.identifier))) return false;
      } else if (!keyTruncated && strcmp(key, "title") == 0) {
        if (!markTask(seen, TASK_TITLE)) return false;
        if (!parseStringField(task.title, sizeof(task.title))) return false;
      } else if (!keyTruncated && strcmp(key, "project_id") == 0) {
        if (!markTask(seen, TASK_PROJECT_ID)) return false;
        if (!parseUnsignedInteger(task.projectId)) return false;
        task.hasProjectId = true;
      } else if (!keyTruncated && strcmp(key, "project") == 0) {
        if (!markTask(seen, TASK_PROJECT)) return false;
        if (!parseStringField(task.project, sizeof(task.project))) return false;
      } else if (!keyTruncated && strcmp(key, "priority") == 0) {
        if (!markTask(seen, TASK_PRIORITY)) return false;
        uint32_t priority = 0;
        if (!parseUnsignedInteger(priority)) return false;
        if (priority > 5U) return fail(ParseError::InvalidNumber);
        task.priority = static_cast<uint8_t>(priority);
      } else if (!keyTruncated && strcmp(key, "due") == 0) {
        if (!markTask(seen, TASK_DUE)) return false;
        if (!parseNullableStringField(task.due, sizeof(task.due))) return false;
      } else if (!keyTruncated && strcmp(key, "state") == 0) {
        if (!markTask(seen, TASK_STATE)) return false;
        if (!parseStringField(task.state, sizeof(task.state))) return false;
      } else if (!skipValue()) {
        return false;
      }

      skipWs();
      if (cursor < end && *cursor == ',') {
        ++cursor;
        skipWs();
        if (cursor < end && *cursor == '}') return fail(ParseError::Syntax);
        continue;
      }
      if (cursor < end && *cursor == '}') {
        ++cursor;
        if ((seen & TASK_ID) == 0 || (seen & TASK_TITLE) == 0 || (seen & TASK_PRIORITY) == 0 ||
            (seen & TASK_STATE) == 0) {
          return fail(ParseError::MissingField);
        }
        return true;
      }
      return fail(ParseError::Syntax);
    }
  }

  bool parseAgentsArray() {
    if (!consume('[')) return false;
    skipWs();
    if (cursor < end && *cursor == ']') {
      ++cursor;
      return true;
    }

    while (true) {
      if (feed.agentCount >= MAX_AGENTS) return fail(ParseError::LimitExceeded);
      Agent& agent = feed.agents[feed.agentCount];
      if (!parseAgentObject(agent)) return false;
      ++feed.agentCount;

      skipWs();
      if (cursor < end && *cursor == ',') {
        ++cursor;
        skipWs();
        if (cursor < end && *cursor == ']') return fail(ParseError::Syntax);
        continue;
      }
      if (cursor < end && *cursor == ']') {
        ++cursor;
        return true;
      }
      return fail(ParseError::Syntax);
    }
  }

  bool parseAgentObject(Agent& agent) {
    if (!consume('{')) return false;
    uint8_t seen = 0;
    skipWs();
    if (cursor < end && *cursor == '}') {
      ++cursor;
      return fail(ParseError::MissingField);
    }

    while (true) {
      char key[32];
      bool keyTruncated = false;
      if (!parseString(key, sizeof(key), keyTruncated, false)) return false;
      if (!consume(':')) return false;

      if (!keyTruncated && strcmp(key, "name") == 0) {
        if (!markAgent(seen, AGENT_NAME)) return false;
        if (!parseStringField(agent.name, sizeof(agent.name))) return false;
      } else if (!keyTruncated && strcmp(key, "state") == 0) {
        if (!markAgent(seen, AGENT_STATE)) return false;
        if (!parseStringField(agent.state, sizeof(agent.state))) return false;
      } else if (!keyTruncated && strcmp(key, "summary") == 0) {
        if (!markAgent(seen, AGENT_SUMMARY)) return false;
        if (!parseStringField(agent.summary, sizeof(agent.summary))) return false;
      } else if (!skipValue()) {
        return false;
      }

      skipWs();
      if (cursor < end && *cursor == ',') {
        ++cursor;
        skipWs();
        if (cursor < end && *cursor == '}') return fail(ParseError::Syntax);
        continue;
      }
      if (cursor < end && *cursor == '}') {
        ++cursor;
        if ((seen & AGENT_NAME) == 0 || (seen & AGENT_STATE) == 0) return fail(ParseError::MissingField);
        return true;
      }
      return fail(ParseError::Syntax);
    }
  }

  bool parseStringField(char* out, const size_t outSize) {
    bool truncated = false;
    if (!parseString(out, outSize, truncated, true)) return false;
    if (truncated) return fail(ParseError::FieldTooLong);
    if (!validUtf8(out)) return fail(ParseError::InvalidUtf8);
    return true;
  }

  bool parseNullableStringField(char* out, const size_t outSize) {
    skipWs();
    if (cursor < end && *cursor == 'n') {
      if (!parseLiteral("null")) return false;
      out[0] = '\0';
      return true;
    }
    return parseStringField(out, outSize);
  }

  bool parseIdField(char* out, const size_t outSize) {
    skipWs();
    if (cursor == end) return fail(ParseError::Syntax);
    if (*cursor == '"') return parseStringField(out, outSize);
    uint32_t id = 0;
    if (!parseUnsignedInteger(id)) return false;
    char tmp[16];
    size_t len = 0;
    uint32_t value = id;
    char reversed[10];
    size_t revLen = 0;
    do {
      reversed[revLen++] = static_cast<char>('0' + (value % 10U));
      value /= 10U;
    } while (value > 0U && revLen < sizeof(reversed));
    while (revLen > 0U) tmp[len++] = reversed[--revLen];
    tmp[len] = '\0';
    if (len >= outSize) return fail(ParseError::FieldTooLong);
    memcpy(out, tmp, len + 1U);
    return true;
  }

  bool parseString(char* out, const size_t outSize, bool& truncated, const bool fieldValue) {
    truncated = false;
    skipWs();
    if (cursor == end || *cursor != '"') return fail(ParseError::InvalidType);
    ++cursor;
    size_t outLen = 0;

    while (cursor < end) {
      const unsigned char c = static_cast<unsigned char>(*cursor++);
      if (c == '"') {
        if (outLen < outSize) {
          out[outLen] = '\0';
        } else if (outSize > 0U) {
          out[outSize - 1U] = '\0';
        }
        return true;
      }
      if (c < 0x20U) return fail(ParseError::Syntax);

      if (c != '\\') {
        appendByte(out, outSize, outLen, static_cast<char>(c), truncated);
        continue;
      }

      if (cursor == end) return fail(ParseError::Syntax);
      const char escaped = *cursor++;
      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          appendByte(out, outSize, outLen, escaped, truncated);
          break;
        case 'b':
          appendByte(out, outSize, outLen, '\b', truncated);
          break;
        case 'f':
          appendByte(out, outSize, outLen, '\f', truncated);
          break;
        case 'n':
          appendByte(out, outSize, outLen, fieldValue ? ' ' : '\n', truncated);
          break;
        case 'r':
          appendByte(out, outSize, outLen, fieldValue ? ' ' : '\r', truncated);
          break;
        case 't':
          appendByte(out, outSize, outLen, fieldValue ? ' ' : '\t', truncated);
          break;
        case 'u': {
          uint32_t cp = 0;
          if (!readHexCodepoint(cp)) return false;
          if (cp >= 0xD800U && cp <= 0xDBFFU) {
            if (end - cursor < 6 || cursor[0] != '\\' || cursor[1] != 'u') return fail(ParseError::Syntax);
            cursor += 2;
            uint32_t low = 0;
            if (!readHexCodepoint(low)) return false;
            if (low < 0xDC00U || low > 0xDFFFU) return fail(ParseError::Syntax);
            cp = 0x10000U + ((cp - 0xD800U) << 10U) + (low - 0xDC00U);
          } else if (cp >= 0xDC00U && cp <= 0xDFFFU) {
            return fail(ParseError::Syntax);
          }

          if (!appendUtf8Escaped(out, outSize, outLen, cp, truncated)) return false;
          break;
        }
        default:
          return fail(ParseError::Syntax);
      }
    }

    return fail(ParseError::Syntax);
  }

  void appendByte(char* out, const size_t outSize, size_t& outLen, const char value, bool& truncated) {
    if (outLen + 1U < outSize) {
      out[outLen++] = value;
      return;
    }
    truncated = true;
  }

  bool appendUtf8Escaped(char* out, const size_t outSize, size_t& outLen, const uint32_t cp, bool& truncated) {
    char tmp[5] = {};
    size_t len = 0;
    if (!appendUtf8(tmp, sizeof(tmp), len, cp)) return fail(ParseError::Syntax);
    for (size_t i = 0; i < len; ++i) appendByte(out, outSize, outLen, tmp[i], truncated);
    return true;
  }

  bool readHexCodepoint(uint32_t& cp) {
    if (end - cursor < 4) return fail(ParseError::Syntax);
    cp = 0;
    for (uint8_t i = 0; i < 4; ++i) {
      uint16_t nibble = 0;
      if (!hexValue(*cursor++, nibble)) return fail(ParseError::Syntax);
      cp = (cp << 4U) | nibble;
    }
    return true;
  }

  bool parseUnsignedInteger(uint32_t& value) {
    skipWs();
    if (cursor == end) return fail(ParseError::Syntax);
    if (*cursor == '-') return fail(ParseError::InvalidNumber);
    if (!isDigit(*cursor)) return fail(ParseError::InvalidType);

    const char* start = cursor;
    uint64_t result = 0;
    if (*cursor == '0') {
      ++cursor;
      if (cursor < end && isDigit(*cursor)) return fail(ParseError::InvalidNumber);
    } else {
      while (cursor < end && isDigit(*cursor)) {
        result = result * 10U + static_cast<uint64_t>(*cursor - '0');
        if (result > 0xFFFFFFFFULL) return fail(ParseError::InvalidNumber);
        ++cursor;
      }
    }

    if (cursor == start) return fail(ParseError::InvalidType);
    if (cursor < end && (*cursor == '.' || *cursor == 'e' || *cursor == 'E')) return fail(ParseError::InvalidNumber);
    value = static_cast<uint32_t>(result);
    return true;
  }

  bool skipValue(const size_t depth = 0) {
    skipWs();
    if (cursor == end) return fail(ParseError::Syntax);
    switch (peekValueKind()) {
      case ValueKind::String:
        return skipString();
      case ValueKind::Number:
        return skipNumber();
      case ValueKind::Object:
        if (depth >= MAX_UNKNOWN_NESTING_DEPTH) return fail(ParseError::NestingTooDeep);
        return skipObject(depth + 1U);
      case ValueKind::Array:
        if (depth >= MAX_UNKNOWN_NESTING_DEPTH) return fail(ParseError::NestingTooDeep);
        return skipArray(depth + 1U);
      case ValueKind::Bool:
        return *cursor == 't' ? parseLiteral("true") : parseLiteral("false");
      case ValueKind::Null:
        return parseLiteral("null");
    }
    return false;
  }

  ValueKind peekValueKind() {
    if (*cursor == '"') return ValueKind::String;
    if (*cursor == '{') return ValueKind::Object;
    if (*cursor == '[') return ValueKind::Array;
    if (*cursor == 't' || *cursor == 'f') return ValueKind::Bool;
    if (*cursor == 'n') return ValueKind::Null;
    return ValueKind::Number;
  }

  bool skipString() {
    char ignored[1];
    bool truncated = false;
    return parseString(ignored, sizeof(ignored), truncated, false);
  }

  bool skipNumber() {
    skipWs();
    if (cursor == end) return fail(ParseError::Syntax);
    if (*cursor == '-') ++cursor;
    if (cursor == end) return fail(ParseError::Syntax);
    if (*cursor == '0') {
      ++cursor;
      if (cursor < end && isDigit(*cursor)) return fail(ParseError::Syntax);
    } else if (*cursor >= '1' && *cursor <= '9') {
      while (cursor < end && isDigit(*cursor)) ++cursor;
    } else {
      return fail(ParseError::InvalidType);
    }

    if (cursor < end && *cursor == '.') {
      ++cursor;
      if (cursor == end || !isDigit(*cursor)) return fail(ParseError::Syntax);
      while (cursor < end && isDigit(*cursor)) ++cursor;
    }

    if (cursor < end && (*cursor == 'e' || *cursor == 'E')) {
      ++cursor;
      if (cursor < end && (*cursor == '+' || *cursor == '-')) ++cursor;
      if (cursor == end || !isDigit(*cursor)) return fail(ParseError::Syntax);
      while (cursor < end && isDigit(*cursor)) ++cursor;
    }
    return true;
  }

  bool parseLiteral(const char* literal) {
    skipWs();
    const size_t len = strlen(literal);
    if (static_cast<size_t>(end - cursor) < len || memcmp(cursor, literal, len) != 0) return fail(ParseError::Syntax);
    cursor += len;
    return true;
  }

  bool skipObject(const size_t depth) {
    if (!consume('{')) return false;
    skipWs();
    if (cursor < end && *cursor == '}') {
      ++cursor;
      return true;
    }
    while (true) {
      if (!skipString()) return false;
      if (!consume(':')) return false;
      if (!skipValue(depth)) return false;
      skipWs();
      if (cursor < end && *cursor == ',') {
        ++cursor;
        skipWs();
        if (cursor < end && *cursor == '}') return fail(ParseError::Syntax);
        continue;
      }
      if (cursor < end && *cursor == '}') {
        ++cursor;
        return true;
      }
      return fail(ParseError::Syntax);
    }
  }

  bool skipArray(const size_t depth) {
    if (!consume('[')) return false;
    skipWs();
    if (cursor < end && *cursor == ']') {
      ++cursor;
      return true;
    }
    while (true) {
      if (!skipValue(depth)) return false;
      skipWs();
      if (cursor < end && *cursor == ',') {
        ++cursor;
        skipWs();
        if (cursor < end && *cursor == ']') return fail(ParseError::Syntax);
        continue;
      }
      if (cursor < end && *cursor == ']') {
        ++cursor;
        return true;
      }
      return fail(ParseError::Syntax);
    }
  }

  const char* cursor;
  const char* const end;
  Feed& feed;
  ParseError error = ParseError::None;
  uint8_t rootSeen = 0;
};

}  // namespace

AtlasFeedJsonParser::AtlasFeedJsonParser() { reset(); }

void AtlasFeedJsonParser::reset() {
  rawLen = 0;
  raw[0] = '\0';
  memset(&parsedFeed, 0, sizeof(parsedFeed));
  error = ParseError::None;
}

bool AtlasFeedJsonParser::feed(const char* data, const size_t len) {
  if (error != ParseError::None) return false;
  if (!data && len > 0U) {
    error = ParseError::Syntax;
    return false;
  }
  if (len > MAX_BODY_BYTES - rawLen) {
    error = ParseError::BodyTooLarge;
    return false;
  }
  if (len > 0U) {
    memcpy(raw + rawLen, data, len);
    rawLen += len;
    raw[rawLen] = '\0';
  }
  return true;
}

bool AtlasFeedJsonParser::finish() {
  if (error != ParseError::None) return false;
  return parseFeedJson(raw, rawLen, parsedFeed, error);
}

const char* AtlasFeedJsonParser::errorName(const ParseError value) {
  switch (value) {
    case ParseError::None:
      return "None";
    case ParseError::BodyTooLarge:
      return "BodyTooLarge";
    case ParseError::Syntax:
      return "Syntax";
    case ParseError::RootNotObject:
      return "RootNotObject";
    case ParseError::DuplicateField:
      return "DuplicateField";
    case ParseError::MissingField:
      return "MissingField";
    case ParseError::UnsupportedSchema:
      return "UnsupportedSchema";
    case ParseError::InvalidType:
      return "InvalidType";
    case ParseError::LimitExceeded:
      return "LimitExceeded";
    case ParseError::FieldTooLong:
      return "FieldTooLong";
    case ParseError::InvalidUtf8:
      return "InvalidUtf8";
    case ParseError::InvalidTimestamp:
      return "InvalidTimestamp";
    case ParseError::InvalidNumber:
      return "InvalidNumber";
    case ParseError::NestingTooDeep:
      return "NestingTooDeep";
    case ParseError::TrailingData:
      return "TrailingData";
  }
  return "Unknown";
}

bool parseFeedJson(const char* data, const size_t len, Feed& feed, ParseError& parseError) {
  if ((!data && len > 0U) || len > MAX_BODY_BYTES) {
    parseError = len > MAX_BODY_BYTES ? ParseError::BodyTooLarge : ParseError::Syntax;
    return false;
  }
  JsonReader reader(data ? data : "", len, feed);
  if (!reader.parse()) {
    parseError = reader.getError();
    if (parseError == ParseError::None) parseError = ParseError::Syntax;
    return false;
  }
  parseError = ParseError::None;
  return true;
}

}  // namespace atlas_feed
