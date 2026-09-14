#include <gtest/gtest.h>

#include "SalverUtil.h"

namespace {

using namespace salver;

TEST(SalverUtil, EditionDateShape) {
  EXPECT_TRUE(isEditionDate("2026-09-14"));
  EXPECT_FALSE(isEditionDate("2026-9-14"));
  EXPECT_FALSE(isEditionDate("2026-09-14.epub"));
  EXPECT_FALSE(isEditionDate("latest"));
  EXPECT_FALSE(isEditionDate(""));
  EXPECT_FALSE(isEditionDate(nullptr));
  EXPECT_FALSE(isEditionDate("../../.."));
}

TEST(SalverUtil, EditionOlderThanCutoff) {
  EXPECT_TRUE(editionOlderThan("2026-09-01.epub", "2026-09-07"));
  EXPECT_FALSE(editionOlderThan("2026-09-07.epub", "2026-09-07"));
  EXPECT_FALSE(editionOlderThan("2026-09-08.epub", "2026-09-07"));
  EXPECT_FALSE(editionOlderThan("latest.epub", "2026-09-07"));
  EXPECT_FALSE(editionOlderThan("2026-09-01.bmp", "2026-09-07"));
  EXPECT_FALSE(editionOlderThan("My Novel - Author.epub", "2026-09-07"));
  EXPECT_FALSE(editionOlderThan(nullptr, "2026-09-07"));
}

TEST(SalverUtil, DateFromEpochIsUtc) {
  EXPECT_EQ(dateFromEpoch(1789344000), "2026-09-14");  // 2026-09-14T00:00:00Z
  EXPECT_EQ(dateFromEpoch(1789343999), "2026-09-13");
}

TEST(SalverUtil, ClampWakeSeconds) {
  EXPECT_EQ(clampWakeSeconds(-5), 60);
  EXPECT_EQ(clampWakeSeconds(0), 60);
  EXPECT_EQ(clampWakeSeconds(59), 60);
  EXPECT_EQ(clampWakeSeconds(3600), 3600);
  EXPECT_EQ(clampWakeSeconds(10 * 86400), 26 * 3600);
}

TEST(SalverUtil, ParseInt64) {
  EXPECT_EQ(parseInt64("", 7), 7);
  EXPECT_EQ(parseInt64("abc", 7), 7);
  EXPECT_EQ(parseInt64("1757808000", 0), 1757808000);
  EXPECT_EQ(parseInt64(" 42", 0), 42);
  EXPECT_EQ(parseInt64("-1", 0), -1);
}

TEST(SalverUtil, StripTrailingSlash) {
  EXPECT_EQ(stripTrailingSlash("http://x/"), "http://x");
  EXPECT_EQ(stripTrailingSlash("http://x//"), "http://x");
  EXPECT_EQ(stripTrailingSlash("http://x"), "http://x");
  EXPECT_EQ(stripTrailingSlash("/"), "");
}

}  // namespace
