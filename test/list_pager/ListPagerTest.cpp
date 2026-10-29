#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <vector>

#include "components/ListPager.h"

namespace fui = freeink::ui;

namespace {
constexpr int16_t kLine = 20;

class FixedTarget final : public fui::DrawTarget {
 public:
  fui::Size measureText(fui::FontId, const char* text, fui::TextStyle) const override {
    return {static_cast<int16_t>(8 * std::strlen(text)), kLine};
  }
  int16_t lineHeight(fui::FontId) const override { return kLine; }
  void fill(fui::Rect, fui::Paint, uint8_t, uint8_t) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(fui::Rect, const char*, fui::TextStyle) override {}
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}
};

// The two Screen calls ListPager::prepare makes.
struct Band {
  fui::Rect content;
  fui::Rect contentRect() const { return content; }
  fui::Rect takeBottom(const int16_t height) {
    content.height = static_cast<int16_t>(content.height - height);
    return {content.x, content.bottom(), content.width, height};
  }
};

// A button-board list on a 200 px band: rows 30 px, subtitle rows 40 px, and
// rows under a section heading 30 + 24 px (+16 px of section gap off the top).
class Pager : public ::testing::Test {
 protected:
  void SetUp() override { device.width = 480; }

  void makeRows(const int count, const bool uneven) {
    items.assign(static_cast<size_t>(count), fui::ListItem{});
    for (int i = 0; i < count; ++i) {
      auto& item = items[static_cast<size_t>(i)];
      item.label = "Row";
      item.actionValue = static_cast<int16_t>(i);
      if (uneven && i % 3 == 1) item.subtitle = "Two lines";
      if (uneven && i % 7 == 4) item.sectionHeading = "Heading";
    }
  }

  // One screen build: the band, the nav's sync, the pager, the list, the feedback;
  // again while the pager walks. Returns the page shown.
  int show(const int selection) {
    nav.requestSelection(selection);
    do {
      Band band{{0, 100, 480, 200}};
      fui::ListProps props;
      props.items = items.data();
      props.count = static_cast<uint16_t>(items.size());
      props.rowHeight = 30;
      props.rowGap = 0;
      props.labelText.maxLines = 1;
      nav.syncToProps(band.content, props.rowHeight, props.rowGap, props.count, props);
      pager.prepare(band, props, nav, props.count, line ? band.content.bottom() - 24 : -1);
      pager.follow(nav, props);
      interactions.clear();
      fui::Frame<64> frame(target, device, input, interactions);
      fui::list(frame, band.contentRect(), props);
      pager.afterLayout(nav);
    } while (nav.consumeRebuildNeeded());
    EXPECT_LE(nav.top, selection);
    EXPECT_LT(selection, nav.top + nav.drawnRows);
    return nav.top;
  }

  FixedTarget target;
  fui::DeviceContext device;
  fui::InputSnapshot input;
  fui::InteractionBuffer<64> interactions;
  std::vector<fui::ListItem> items;
  fui::ListNav nav;
  ListPager pager;
  bool line = true;
};

TEST_F(Pager, UnevenRowsPageWithoutOverlapOrGaps) {
  makeRows(40, true);
  std::vector<int> forward;
  std::vector<int> drawn;
  for (int row = 0; row < 40; ++row) {
    const int top = show(row);
    if (forward.empty() || forward.back() != top) {
      // A new page starts where the one before it ended.
      if (!forward.empty()) EXPECT_EQ(top, forward.back() + drawn.back());
      forward.push_back(top);
      drawn.push_back(nav.drawnRows);
    }
  }
  EXPECT_EQ(forward.back() + drawn.back(), 40);
  // Pages hold different numbers of rows.
  EXPECT_LT(*std::min_element(drawn.begin(), drawn.end()), *std::max_element(drawn.begin(), drawn.end()));

  std::vector<int> backward;
  for (int row = 39; row >= 0; --row) {
    const int top = show(row);
    if (backward.empty() || backward.back() != top) backward.push_back(top);
  }
  EXPECT_EQ(std::vector<int>(backward.rbegin(), backward.rend()), forward);
}

TEST_F(Pager, PageLineNumbersThePagesFromRowZero) {
  makeRows(40, true);
  std::set<int> tops;
  for (int row = 0; row < 40; ++row) tops.insert(show(row));
  const int pages = static_cast<int>(tops.size());
  int index = 0;
  for (const int top : tops) {
    show(top);
    int page = 0;
    int total = 0;
    ASSERT_TRUE(pager.position(page, total));
    EXPECT_EQ(page, ++index);
    EXPECT_EQ(total, pages);
  }
}

TEST_F(Pager, JumpsAndWrapLandOnTheSamePages) {
  makeRows(40, true);
  std::vector<int> pageOf(40);
  for (int row = 0; row < 40; ++row) pageOf[static_cast<size_t>(row)] = show(row);

  // Fresh pager: the first build walks from row 0.
  pager = ListPager{};
  nav.reset(0);
  EXPECT_EQ(show(39), pageOf[39]);
  EXPECT_EQ(show(0), 0);
  EXPECT_EQ(show(25), pageOf[25]);
  EXPECT_EQ(show(39), pageOf[39]);
  EXPECT_EQ(show(3), pageOf[3]);
}

TEST_F(Pager, UniformRowsPageByWholePages) {
  makeRows(23, false);
  // 200 - 24 px of page line = 176 px: five 30 px rows a page.
  for (int row = 0; row < 23; ++row) EXPECT_EQ(show(row), row / 5 * 5);
  int page = 0;
  int pages = 0;
  show(22);
  ASSERT_TRUE(pager.position(page, pages));
  EXPECT_EQ(page, 5);
  EXPECT_EQ(pages, 5);
}

TEST_F(Pager, FitsOnePageWithoutALine) {
  makeRows(4, false);
  EXPECT_EQ(show(3), 0);
  int page = 0;
  int pages = 0;
  EXPECT_FALSE(pager.position(page, pages));
}

TEST_F(Pager, PagesWithoutALineToo) {
  line = false;
  makeRows(40, true);
  std::vector<int> forward;
  for (int row = 0; row < 40; ++row) {
    const int top = show(row);
    if (forward.empty() || forward.back() != top) forward.push_back(top);
  }
  std::vector<int> backward;
  for (int row = 39; row >= 0; --row) {
    const int top = show(row);
    if (backward.empty() || backward.back() != top) backward.push_back(top);
  }
  EXPECT_EQ(std::vector<int>(backward.rbegin(), backward.rend()), forward);
}
}  // namespace
