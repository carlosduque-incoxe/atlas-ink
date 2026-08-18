#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "AtlasFeedJsonParser.h"

namespace {

constexpr const char* kValidFeed = R"({
  "schema": 1,
  "generated_at": "2026-08-18T20:00:00Z",
  "etag": "sha256-opaque",
  "tasks": [
    {
      "id": 123,
      "identifier": "NIL-8",
      "title": "Restablecer alimentación",
      "project_id": 9,
      "priority": 4,
      "due": "2026-08-19T08:30:00Z",
      "state": "open"
    }
  ],
  "agents": [
    {
      "name": "Atlas correo",
      "state": "blocked",
      "summary": "Permiso de escritura pendiente"
    }
  ]
})";

atlas_feed::AtlasFeedJsonParser parseWhole(const char* json) {
  atlas_feed::AtlasFeedJsonParser parser;
  EXPECT_TRUE(parser.feed(json, strlen(json)));
  parser.finish();
  return parser;
}

atlas_feed::AtlasFeedJsonParser parseChunked(const char* json, const size_t chunkSize) {
  atlas_feed::AtlasFeedJsonParser parser;
  const size_t len = strlen(json);
  for (size_t off = 0; off < len; off += chunkSize) {
    const size_t n = std::min(chunkSize, len - off);
    if (!parser.feed(json + off, n)) break;
  }
  parser.finish();
  return parser;
}

void expectError(const char* json, const atlas_feed::ParseError expected) {
  atlas_feed::AtlasFeedJsonParser parser;
  EXPECT_TRUE(parser.feed(json, strlen(json)));
  EXPECT_FALSE(parser.finish());
  EXPECT_EQ(parser.getError(), expected) << atlas_feed::AtlasFeedJsonParser::errorName(parser.getError());
}

std::string validWithTasks(const size_t count) {
  std::string json = R"({"schema":1,"generated_at":"2026-08-18T20:00:00Z","tasks":[)";
  for (size_t i = 0; i < count; ++i) {
    if (i > 0) json += ",";
    json += R"({"id":)";
    json += std::to_string(i + 1);
    json += R"(,"title":"Tarea","priority":1,"state":"open"})";
  }
  json += R"(],"agents":[]})";
  return json;
}

}  // namespace

TEST(AtlasFeedJsonParser, ValidBodyProjectsExpectedFields) {
  auto parser = parseWhole(kValidFeed);
  ASSERT_EQ(parser.getError(), atlas_feed::ParseError::None);
  const auto& feed = parser.getFeed();
  EXPECT_STREQ(feed.generatedAt, "2026-08-18T20:00:00Z");
  EXPECT_STREQ(feed.etag, "sha256-opaque");
  ASSERT_EQ(feed.taskCount, 1U);
  EXPECT_STREQ(feed.tasks[0].id, "123");
  EXPECT_STREQ(feed.tasks[0].identifier, "NIL-8");
  EXPECT_STREQ(feed.tasks[0].title, "Restablecer alimentación");
  EXPECT_TRUE(feed.tasks[0].hasProjectId);
  EXPECT_EQ(feed.tasks[0].projectId, 9U);
  EXPECT_EQ(feed.tasks[0].priority, 4U);
  EXPECT_STREQ(feed.tasks[0].due, "2026-08-19T08:30:00Z");
  EXPECT_STREQ(feed.tasks[0].state, "open");
  ASSERT_EQ(feed.agentCount, 1U);
  EXPECT_STREQ(feed.agents[0].name, "Atlas correo");
  EXPECT_STREQ(feed.agents[0].state, "blocked");
  EXPECT_STREQ(feed.agents[0].summary, "Permiso de escritura pendiente");
}

TEST(AtlasFeedJsonParser, ByteByByteAndIrregularChunkingMatch) {
  auto whole = parseWhole(kValidFeed);
  auto one = parseChunked(kValidFeed, 1);
  auto seven = parseChunked(kValidFeed, 7);

  ASSERT_EQ(whole.getError(), atlas_feed::ParseError::None);
  ASSERT_EQ(one.getError(), atlas_feed::ParseError::None);
  ASSERT_EQ(seven.getError(), atlas_feed::ParseError::None);
  EXPECT_STREQ(one.getFeed().tasks[0].title, whole.getFeed().tasks[0].title);
  EXPECT_STREQ(seven.getFeed().agents[0].summary, whole.getFeed().agents[0].summary);
}

TEST(AtlasFeedJsonParser, MalformedAndTruncatedJsonRejected) {
  expectError(R"({"schema":1,"generated_at":"x","tasks":[],"agents":[])", atlas_feed::ParseError::Syntax);
  expectError(R"({"schema":1,"generated_at":"x","tasks":[,],"agents":[]})", atlas_feed::ParseError::Syntax);
  expectError(R"([])", atlas_feed::ParseError::RootNotObject);
}

TEST(AtlasFeedJsonParser, WrongMissingAndDuplicateSchemaRejected) {
  expectError(R"({"schema":2,"generated_at":"x","tasks":[],"agents":[]})", atlas_feed::ParseError::UnsupportedSchema);
  expectError(R"({"generated_at":"x","tasks":[],"agents":[]})", atlas_feed::ParseError::MissingField);
  expectError(R"({"schema":1,"schema":1,"generated_at":"x","tasks":[],"agents":[]})",
              atlas_feed::ParseError::DuplicateField);
}

TEST(AtlasFeedJsonParser, RequiredTopLevelFieldsMatterButEtagIsOptional) {
  auto parser = parseWhole(R"({"schema":1,"generated_at":"x","tasks":[],"agents":[]})");
  EXPECT_EQ(parser.getError(), atlas_feed::ParseError::None);
  EXPECT_STREQ(parser.getFeed().etag, "");

  expectError(R"({"schema":1,"tasks":[],"agents":[]})", atlas_feed::ParseError::MissingField);
  expectError(R"({"schema":1,"generated_at":"x","agents":[]})", atlas_feed::ParseError::MissingField);
  expectError(R"({"schema":1,"generated_at":"x","tasks":[]})", atlas_feed::ParseError::MissingField);
}

TEST(AtlasFeedJsonParser, RejectsMoreThanFiveTasksAndAgents) {
  const std::string sixTasks = validWithTasks(6);
  expectError(sixTasks.c_str(), atlas_feed::ParseError::LimitExceeded);

  const char* sixAgents =
      R"({"schema":1,"generated_at":"x","tasks":[],"agents":[{"name":"a","state":"ok"},{"name":"b","state":"ok"},{"name":"c","state":"ok"},{"name":"d","state":"ok"},{"name":"e","state":"ok"},{"name":"f","state":"ok"}]})";
  expectError(sixAgents, atlas_feed::ParseError::LimitExceeded);
}

TEST(AtlasFeedJsonParser, RejectsOversizedFieldsAndBody) {
  std::string title(atlas_feed::MAX_TASK_TITLE_LEN + 1, 'x');
  std::string json = R"({"schema":1,"generated_at":"x","tasks":[{"id":1,"title":")" + title +
                     R"(","priority":1,"state":"open"}],"agents":[]})";
  expectError(json.c_str(), atlas_feed::ParseError::FieldTooLong);

  atlas_feed::AtlasFeedJsonParser parser;
  std::string body(atlas_feed::MAX_BODY_BYTES + 1, ' ');
  EXPECT_FALSE(parser.feed(body.data(), body.size()));
  EXPECT_EQ(parser.getError(), atlas_feed::ParseError::BodyTooLarge);
}

TEST(AtlasFeedJsonParser, UnknownNestedObjectsAndArraysAreIgnored) {
  const char* json = R"({
    "schema": 1,
    "generated_at": "x",
    "unknown": [{"deep": {"value": [1, true, null, "ok"]}}],
    "tasks": [{
      "id": "NIL-8",
      "title": "Tarea",
      "priority": 3,
      "state": "open",
      "labels": [{"name": "infra"}]
    }],
    "agents": [{"name": "Atlas", "state": "ok", "extra": {"jobs": [1, 2]}}]
  })";

  auto parser = parseWhole(json);
  ASSERT_EQ(parser.getError(), atlas_feed::ParseError::None);
  ASSERT_EQ(parser.getFeed().taskCount, 1U);
  EXPECT_STREQ(parser.getFeed().tasks[0].id, "NIL-8");
  ASSERT_EQ(parser.getFeed().agentCount, 1U);
  EXPECT_STREQ(parser.getFeed().agents[0].name, "Atlas");
}

TEST(AtlasFeedJsonParser, RejectsExcessiveUnknownNesting) {
  std::string nested(17, '[');
  nested += "0";
  nested.append(17, ']');
  const std::string json = R"({"schema":1,"generated_at":"x","unknown":)" + nested +
                           R"(,"tasks":[],"agents":[]})";
  expectError(json.c_str(), atlas_feed::ParseError::NestingTooDeep);
}

TEST(AtlasFeedJsonParser, Utf8BoundariesAndEscapes) {
  const char* json =
      R"({"schema":1,"generated_at":"x","tasks":[{"id":1,"title":"Niño \uD83D\uDCA1","priority":1,"state":"open"}],"agents":[{"name":"Correo","state":"ok","summary":"Señal"}]})";
  auto parser = parseChunked(json, 2);
  ASSERT_EQ(parser.getError(), atlas_feed::ParseError::None);
  EXPECT_STREQ(parser.getFeed().tasks[0].title, "Niño 💡");
  EXPECT_STREQ(parser.getFeed().agents[0].summary, "Señal");

  const char invalid[] =
      "{\"schema\":1,\"generated_at\":\"x\",\"tasks\":[{\"id\":1,\"title\":\"bad \xC3"
      "\",\"priority\":1,\"state\":\"open\"}],\"agents\":[]}";
  expectError(invalid, atlas_feed::ParseError::InvalidUtf8);
}

TEST(AtlasFeedJsonParser, AcceptsBackendMaximumUnicodeTitle) {
  std::string validTitle;
  validTitle.reserve(320);
  for (size_t i = 0; i < 160; ++i) validTitle += "á";
  const std::string json =
      R"({"schema":1,"generated_at":"x","tasks":[{"id":1,"title":")" + validTitle +
      R"(","priority":3,"state":"open"}],"agents":[]})";

  auto parser = parseWhole(json.c_str());
  ASSERT_EQ(parser.getError(), atlas_feed::ParseError::None);
  EXPECT_EQ(strlen(parser.getFeed().tasks[0].title), 320U);
}

TEST(AtlasFeedJsonParser, InvalidTaskTypesPriorityAndIdRejected) {
  expectError(R"({"schema":1,"generated_at":"x","tasks":[{"id":{"bad":1},"title":"t","priority":1,"state":"open"}],"agents":[]})",
              atlas_feed::ParseError::InvalidType);
  expectError(R"({"schema":1,"generated_at":"x","tasks":[{"id":1,"title":"t","priority":6,"state":"open"}],"agents":[]})",
              atlas_feed::ParseError::InvalidNumber);
  expectError(R"({"schema":1,"generated_at":"x","tasks":[{"id":-1,"title":"t","priority":1,"state":"open"}],"agents":[]})",
              atlas_feed::ParseError::InvalidNumber);
  expectError(R"({"schema":1,"generated_at":"x","tasks":[{"id":1,"title":9,"priority":1,"state":"open"}],"agents":[]})",
              atlas_feed::ParseError::InvalidType);
}

TEST(AtlasFeedJsonParser, DuplicateFieldsWhereAmbiguousAreRejected) {
  expectError(R"({"schema":1,"generated_at":"x","tasks":[{"id":1,"id":2,"title":"t","priority":1,"state":"open"}],"agents":[]})",
              atlas_feed::ParseError::DuplicateField);
  expectError(R"({"schema":1,"generated_at":"x","tasks":[],"agents":[{"name":"a","state":"ok","state":"blocked"}]})",
              atlas_feed::ParseError::DuplicateField);
}

TEST(AtlasFeedJsonParser, ResetAllowsReuseAfterError) {
  atlas_feed::AtlasFeedJsonParser parser;
  const char* bad = R"({"schema":2,"generated_at":"x","tasks":[],"agents":[]})";
  EXPECT_TRUE(parser.feed(bad, strlen(bad)));
  EXPECT_FALSE(parser.finish());
  EXPECT_EQ(parser.getError(), atlas_feed::ParseError::UnsupportedSchema);

  parser.reset();
  EXPECT_TRUE(parser.feed(kValidFeed, strlen(kValidFeed)));
  EXPECT_TRUE(parser.finish());
  EXPECT_EQ(parser.getError(), atlas_feed::ParseError::None);
  EXPECT_EQ(parser.getFeed().taskCount, 1U);
}
