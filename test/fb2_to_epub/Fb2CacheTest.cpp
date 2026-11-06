#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <string>

#include "Fb2Cache.h"
#include "Fb2ToEpub.h"

namespace {

// A cache directory in memory. Each change is one step; past `budget` steps the
// power is gone and nothing more happens.
class FakeStore : public Fb2Cache::Store {
 public:
  std::map<std::string, std::string> files;  // name -> what it was made from
  int budget = -1;
  bool convertFails = false;

  bool exists(const char* name) override { return files.count(name) != 0; }
  bool remove(const char* name) override { return step() && files.erase(name) == 1; }
  bool rename(const char* from, const char* to) override {
    if (!files.count(from) || !step()) return false;
    files[to] = files[from];
    files.erase(from);
    return true;
  }
  bool convert(const char* name) override {
    if (convertFails || !step()) return false;
    files[name] = "new";
    return true;
  }
  bool clearDerived() override {
    for (const char* derived : {"book.bin", "sections", "img_0_0.jpg"}) {
      if (files.count(derived) && !(step() && files.erase(derived))) return false;
    }
    return true;
  }
  float savedFraction(const char* archive) override {
    // From book.bin, or from the old archive's own directory.
    return files.count("progress.bin") && (files.count("book.bin") || files.count(archive)) ? 0.4f : -1.0f;
  }
  bool savePending(const float fraction, const uint8_t version) override {
    if (!step()) return false;
    files["fb2.pos"] = std::to_string(fraction) + " v" + std::to_string(version);
    return true;
  }

 private:
  bool step() {
    if (budget == 0) return false;
    if (budget > 0) budget--;
    return true;
  }
};

// A book read once by the first converter: its archive and what the reader built from it.
FakeStore openedByOldConverter() {
  FakeStore store;
  for (const char* name : {"fb2.epub", "book.bin", "sections", "img_0_0.jpg"}) store.files[name] = "old";
  store.files["progress.bin"] = "old";
  return store;
}

std::string prepare(FakeStore& store, const bool mayConvert) {
  char name[16];
  const char* archive = Fb2Cache::prepare(store, mayConvert, name, sizeof(name));
  return archive ? archive : "";
}

TEST(Fb2Cache, NamesCarryTheVersion) {
  char name[16];
  EXPECT_STREQ(Fb2Cache::archiveName(1, name, sizeof(name)), "fb2.epub");
  EXPECT_STREQ(Fb2Cache::archiveName(2, name, sizeof(name)), "fb2.v2.epub");
  EXPECT_EQ(Fb2Cache::archiveVersion("fb2.epub"), 1);
  EXPECT_EQ(Fb2Cache::archiveVersion("fb2.v2.epub"), 2);
  EXPECT_EQ(Fb2Cache::archiveVersion("fb2.v2.epub.tmp"), 0);
  EXPECT_EQ(Fb2Cache::archiveVersion(Fb2Cache::NEXT), 0);
  EXPECT_EQ(Fb2Cache::archiveVersion("book.bin"), 0);
}

TEST(Fb2Cache, OnlyTheReaderConverts) {
  FakeStore fresh;
  EXPECT_EQ(prepare(fresh, false), "");
  EXPECT_TRUE(fresh.files.empty());

  // Sleep screen, home, library: the old archive as it is.
  FakeStore old = openedByOldConverter();
  const auto before = old.files;
  EXPECT_EQ(prepare(old, false), "fb2.epub");
  EXPECT_EQ(old.files, before);

  EXPECT_EQ(prepare(fresh, true), "fb2.v2.epub");
  EXPECT_EQ(fresh.files["fb2.v2.epub"], "new");
}

TEST(Fb2Cache, MigratesKeepingThePosition) {
  FakeStore store = openedByOldConverter();
  EXPECT_EQ(prepare(store, true), "fb2.v2.epub");
  const std::map<std::string, std::string> expected = {
      {"fb2.v2.epub", "new"}, {"progress.bin", "old"}, {"fb2.pos", "0.400000 v1"}};
  EXPECT_EQ(store.files, expected);
}

TEST(Fb2Cache, AFailedConversionLeavesTheOldBookReadable) {
  FakeStore store = openedByOldConverter();
  store.convertFails = true;
  EXPECT_EQ(prepare(store, true), "fb2.epub");
  EXPECT_EQ(store.files["book.bin"], "old");
  EXPECT_EQ(store.files["fb2.pos"], "0.400000 v1");
  EXPECT_EQ(store.files.count("fb2.v2.epub"), 0u);
}

TEST(Fb2Cache, SurvivesPowerLossAtEveryStep) {
  for (int steps = 0;; steps++) {
    FakeStore store = openedByOldConverter();
    store.budget = steps;
    const std::string first = prepare(store, true);
    const bool finished = store.budget != 0;
    SCOPED_TRACE("power lost after step " + std::to_string(steps));

    // Whatever the reader would open now, the caches beside it were built from it.
    store.budget = -1;
    const std::string now = prepare(store, false);
    ASSERT_FALSE(now.empty());
    const std::string source = now == "fb2.epub" ? "old" : "new";
    for (const char* derived : {"book.bin", "sections", "img_0_0.jpg"}) {
      if (store.files.count(derived)) EXPECT_EQ(store.files[derived], source) << derived;
    }

    // Opening it again finishes the migration where the position still applies.
    EXPECT_EQ(prepare(store, true), "fb2.v2.epub");
    EXPECT_EQ(store.files.count("fb2.epub"), 0u);
    EXPECT_EQ(store.files.count(Fb2Cache::NEXT), 0u);
    EXPECT_EQ(store.files["fb2.pos"], "0.400000 v1");
    EXPECT_EQ(store.files.count("book.bin"), 0u);
    if (finished) {
      EXPECT_EQ(first, "fb2.v2.epub");
      break;
    }
  }
}

TEST(Fb2Cache, CleansUpAfterAnInterruptedRename) {
  FakeStore store;
  store.files = {{"fb2.epub", "old"}, {"fb2.v2.epub", "new"}, {"fb2.pos", "0.4 v1"}};
  EXPECT_EQ(prepare(store, false), "fb2.v2.epub");
  EXPECT_EQ(store.files.count("fb2.epub"), 0u);
  EXPECT_EQ(store.files["fb2.pos"], "0.4 v1");
}

}  // namespace
