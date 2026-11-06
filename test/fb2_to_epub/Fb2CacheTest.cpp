#include <gtest/gtest.h>

#include <algorithm>
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
  std::map<std::string, std::string> files;  // name -> what it was made from, or its text
  int budget = -1;
  bool convertFails = false;
  bool clearFails = false;
  int conversions = 0;

  bool exists(const char* name) override { return files.count(name) != 0; }
  bool remove(const char* name) override { return step() && files.erase(name) == 1; }
  bool rename(const char* from, const char* to) override {
    if (!files.count(from) || !step()) return false;
    files[to] = files[from];
    files.erase(from);
    return true;
  }
  bool convert(const char* name) override {
    conversions++;
    if (convertFails || !step()) return false;
    files[name] = "new";
    return true;
  }
  bool clearDerived() override {
    if (clearFails) return false;
    for (auto it = files.begin(); it != files.end();) {
      if (Fb2Cache::survivesClear(it->first.c_str())) {
        ++it;
      } else if (step()) {
        it = files.erase(it);
      } else {
        return false;
      }
    }
    return true;
  }
  float savedFraction(const char* archive) override {
    // From book.bin, or from the old archive's own directory.
    return files.count("progress.bin") && (files.count("book.bin") || files.count(archive)) ? 0.4f : -1.0f;
  }
  size_t read(const char* name, char* buf, const size_t size) override {
    const auto it = files.find(name);
    if (it == files.end()) return 0;
    const size_t n = std::min(size, it->second.size());
    memcpy(buf, it->second.data(), n);
    return n;
  }
  bool write(const char* name, const char* data, const size_t len) override {
    if (!step()) return false;
    files[name] = std::string(data, len);
    return true;
  }

 private:
  bool step() {
    if (budget == 0) return false;
    if (budget > 0) budget--;
    return true;
  }
};

const char* const DERIVED[] = {"book.bin", "sections", "img_0_0.jpg"};

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

std::string archive(const uint8_t version) {
  char name[16];
  return Fb2Cache::archiveName(version, name, sizeof(name));
}

const std::string CURRENT = archive(Fb2ToEpub::FORMAT_VERSION);
const std::string NEWER = archive(Fb2ToEpub::FORMAT_VERSION + 1);
const std::string ATTEMPT = [] {
  char name[16];
  return std::string(Fb2Cache::attemptName(Fb2ToEpub::FORMAT_VERSION, name, sizeof(name)));
}();
const std::string PENDING = "0.400000 1\n";

TEST(Fb2Cache, NamesCarryTheVersion) {
  char name[16];
  EXPECT_STREQ(Fb2Cache::archiveName(1, name, sizeof(name)), "fb2.epub");
  EXPECT_STREQ(Fb2Cache::archiveName(2, name, sizeof(name)), "fb2.v2.epub");
  EXPECT_STREQ(Fb2Cache::attemptName(2, name, sizeof(name)), "fb2.v2.try");
  EXPECT_EQ(Fb2Cache::archiveVersion("fb2.epub"), 1);
  EXPECT_EQ(Fb2Cache::archiveVersion("fb2.v2.epub"), 2);
  EXPECT_EQ(Fb2Cache::archiveVersion("fb2.v2.epub.tmp"), 0);
  EXPECT_EQ(Fb2Cache::archiveVersion(Fb2Cache::NEXT), 0);
  EXPECT_EQ(Fb2Cache::archiveVersion("book.bin"), 0);
  for (const char* kept :
       {"fb2.epub", "fb2.v2.epub", "fb2.next.epub", "fb2.v2.try", "fb2.pos", "fb2.src", "progress.bin"}) {
    EXPECT_TRUE(Fb2Cache::survivesClear(kept)) << kept;
  }
  for (const char* derived : {"book.bin", "sections", "img_0_0.jpg", "fb2.next.epub.tmp", "thumb_225.bmp"}) {
    EXPECT_FALSE(Fb2Cache::survivesClear(derived)) << derived;
  }
}

TEST(Fb2Cache, OnlyTheReaderConverts) {
  FakeStore fresh;
  EXPECT_EQ(prepare(fresh, false), "");
  EXPECT_TRUE(fresh.files.empty());

  // Sleep screen, home, library: the old archive as it is.
  FakeStore old = openedByOldConverter();
  EXPECT_EQ(prepare(old, false), "fb2.epub");
  EXPECT_EQ(old.files["book.bin"], "old");
  EXPECT_EQ(old.files["fb2.src"], "fb2.epub");
  EXPECT_EQ(old.conversions, 0);

  EXPECT_EQ(prepare(fresh, true), CURRENT);
  EXPECT_EQ(fresh.files[CURRENT], "new");
  EXPECT_EQ(fresh.files["fb2.src"], CURRENT);
}

TEST(Fb2Cache, MigratesKeepingThePosition) {
  FakeStore store = openedByOldConverter();
  EXPECT_EQ(prepare(store, true), CURRENT);
  const std::map<std::string, std::string> expected = {
      {CURRENT, "new"}, {"progress.bin", "old"}, {"fb2.pos", PENDING}, {"fb2.src", CURRENT}};
  EXPECT_EQ(store.files, expected);
}

TEST(Fb2Cache, MigratesFromThePreviousVersion) {
  const uint8_t previous = Fb2ToEpub::FORMAT_VERSION - 1;
  FakeStore store;
  for (const char* name : DERIVED) store.files[name] = "old";
  store.files[archive(previous)] = "old";
  store.files["progress.bin"] = "old";
  store.files["fb2.src"] = archive(previous);
  // Home and the sleep screen keep reading it; the reader converts, keeping the position.
  EXPECT_EQ(prepare(store, false), archive(previous));
  EXPECT_EQ(store.conversions, 0);
  EXPECT_EQ(prepare(store, true), CURRENT);
  const std::map<std::string, std::string> expected = {{CURRENT, "new"},
                                                       {"progress.bin", "old"},
                                                       {"fb2.pos", "0.400000 " + std::to_string(previous) + "\n"},
                                                       {"fb2.src", CURRENT}};
  EXPECT_EQ(store.files, expected);
}

TEST(Fb2Cache, AFailedMigrationIsNotRetriedOnEveryOpen) {
  FakeStore store = openedByOldConverter();
  store.convertFails = true;  // a full card, say
  EXPECT_EQ(prepare(store, true), "fb2.epub");
  EXPECT_EQ(store.files["book.bin"], "old");
  EXPECT_EQ(store.files["fb2.pos"], PENDING);
  EXPECT_EQ(store.files.count(ATTEMPT), 1u);

  // A conversion that died leaves its partial file; it goes without converting again.
  store.convertFails = false;
  store.files[Fb2Cache::NEXT_TMP] = "part";
  EXPECT_EQ(prepare(store, true), "fb2.epub");
  EXPECT_EQ(store.conversions, 1);
  EXPECT_EQ(store.files.count(Fb2Cache::NEXT_TMP), 0u);

  // Deleting the cache is the retry: here, without the marker.
  store.files.erase(ATTEMPT);
  EXPECT_EQ(prepare(store, true), CURRENT);
  EXPECT_EQ(store.files.count(ATTEMPT), 0u);
}

TEST(Fb2Cache, SurvivesPowerLossAtEveryStep) {
  for (int steps = 0;; steps++) {
    FakeStore store = openedByOldConverter();
    store.budget = steps;
    const std::string first = prepare(store, true);
    const bool finished = store.budget != 0;
    SCOPED_TRACE("power lost after step " + std::to_string(steps));

    // Whatever the reader opens now, the caches beside it were built from it. Past the
    // attempt marker (a conversion that may have died for want of heap) that is the old
    // archive, without converting again.
    store.budget = -1;
    const int conversions = store.conversions;
    const std::string now = prepare(store, true);
    ASSERT_FALSE(now.empty());
    const std::string source = now == "fb2.epub" ? "old" : "new";
    for (const char* derived : DERIVED) {
      if (store.files.count(derived)) EXPECT_EQ(store.files[derived], source) << derived;
    }
    if (store.files.count(ATTEMPT)) EXPECT_EQ(store.conversions, conversions);

    // Deleting the attempt, as «Delete cache» does, lets the next open finish with the position.
    store.files.erase(ATTEMPT);
    EXPECT_EQ(prepare(store, true), CURRENT);
    EXPECT_EQ(store.files.count("fb2.epub"), 0u);
    EXPECT_EQ(store.files.count(Fb2Cache::NEXT), 0u);
    EXPECT_EQ(store.files["fb2.pos"], PENDING);
    EXPECT_EQ(store.files.count("book.bin"), 0u);
    EXPECT_EQ(store.files["fb2.src"], CURRENT);
    if (finished) {
      EXPECT_EQ(first, CURRENT);
      break;
    }
  }
}

TEST(Fb2Cache, CleansUpAfterAnInterruptedRename) {
  FakeStore store;
  store.files = {{"fb2.epub", "old"}, {CURRENT, "new"},        {"fb2.pos", PENDING},
                 {ATTEMPT, ""},       {Fb2Cache::NEXT, "new"}, {Fb2Cache::NEXT_TMP, "part"},
                 {"fb2.src", CURRENT}};
  EXPECT_EQ(prepare(store, false), CURRENT);
  const std::map<std::string, std::string> expected = {{CURRENT, "new"}, {"fb2.pos", PENDING}, {"fb2.src", CURRENT}};
  EXPECT_EQ(store.files, expected);
}

TEST(Fb2Cache, ClearsCachesBuiltFromAnotherArchive) {
  // Converted by another board's firmware, then read here from a different archive.
  FakeStore store;
  store.files = {{CURRENT, "new"}, {"book.bin", "v3"}, {"sections", "v3"}, {"fb2.src", NEWER}};
  // A clear that fails leaves the record as it was, to be tried again.
  store.clearFails = true;
  EXPECT_EQ(prepare(store, false), CURRENT);
  EXPECT_EQ(store.files["fb2.src"], NEWER);
  store.clearFails = false;
  EXPECT_EQ(prepare(store, false), CURRENT);
  const std::map<std::string, std::string> expected = {{CURRENT, "new"}, {"fb2.src", CURRENT}};
  EXPECT_EQ(store.files, expected);
}

}  // namespace
