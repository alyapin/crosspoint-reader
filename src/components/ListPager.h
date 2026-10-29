#pragma once
#include <FreeInkApp.h>

#include <algorithm>
#include <cstdint>

// Button boards page a list by whole rows: no preview of the next row, no
// scroll track, one page on screen at a time. Pages are laid out from row 0,
// each starting where the one before it ended, so uneven rows (wrapped labels,
// subtitles, section headings) neither overlap nor skip. The pager learns the
// page starts from the list's layout feedback: a page it has not laid out yet,
// or the page count, costs one more build per page (ListNav::rebuildNeeded).
// Render task only.
class ListPager {
 public:
  // Page starts remembered; a longer list pages on from a row the pager could
  // not place and numbers its pages by estimate.
  static constexpr int kMaxPages = 64;

  // After Screen::syncListViewport (props resolved). Reserves the band below
  // lineTop for the page line when the rows overflow; lineTop < 0: no line.
  template <typename Screen>
  void prepare(Screen& screen, freeink::ui::ListProps& props, const freeink::ui::ListNav& nav, const int count,
               const int lineTop) {
    props.partialTrailingRow = false;
    props.scrollIndicator = false;
    const freeink::ui::Rect band = screen.contentRect();
    const bool overflows =
        known_ > 1 || count > freeink::ui::listVisibleRows(band, props.rowHeight, props.rowGap) ||
        (nav.trusts(count) && (nav.top > 0 || nav.top + nav.drawnRows < count));
    if (lineTop >= 0 && overflows && band.bottom() > lineTop) {
      screen.takeBottom(static_cast<int16_t>(band.bottom() - lineTop));
    }
    lineWanted_ = lineTop >= 0;
    lineReserved_ = lineTop >= 0 && overflows;
    const freeink::ui::Rect rows = screen.contentRect();
    if (count != count_ || &nav != nav_ || rows.width != band_.width || rows.height != band_.height) {
      reset(count, &nav, rows);
    }
  }

  // After prepare: a selection moved by keys, or kept across a reload, opens
  // the page holding it. The pager owns the viewport, so the nav's own follow
  // stays off.
  void follow(freeink::ui::ListNav& nav, freeink::ui::ListProps& props) {
    const bool moved = nav.followPending || fresh_;
    nav.followPending = false;
    fresh_ = false;
    const int sel = props.selectedIndex;
    if (moved && sel >= 0 && !onPage(sel)) {
      const int page = pageOf(sel);
      if (page >= 0) {
        top_ = starts_[page];
        target_ = -1;
      } else if (offGrid_ || known_ == 0) {
        // Past the remembered pages: the selection opens the page going down
        // and closes it going up.
        top_ = sel < top_ ? std::max(0, sel - std::max(1, drawn_) + 1) : sel;
      } else {
        // Past the pages laid out so far: walk on from the last one.
        top_ = starts_[known_ - 1];
        target_ = sel;
      }
      home_ = -1;
    }
    nav.top = top_;
    props.topIndex = static_cast<uint16_t>(top_);
  }

  // After list(): records the page just laid out and, while walking to the
  // selection's page or (for the page line) to the last page, asks for the
  // next build.
  void afterLayout(freeink::ui::ListNav& nav) {
    const int drawn = nav.top == top_ ? nav.drawnRows : 0;
    if (count_ <= 0 || drawn <= 0) return;
    drawn_ = drawn;
    const int page = indexOf(top_);
    const int next = top_ + drawn;
    if (page >= 0) record(page, next);
    int go = -1;
    if (target_ >= next && next < count_) {
      if (page >= 0 && page + 1 < known_) {
        go = next;
      } else {
        // Too far to remember: page on from the selection itself.
        offGrid_ = true;
        go = target_;
        target_ = -1;
      }
    } else {
      target_ = -1;
      if (lineWanted_ && lineReserved_ && !complete_ && !offGrid_ && known_ < kMaxPages && page >= 0) {
        if (home_ < 0) home_ = top_;
        go = starts_[known_ - 1];
      } else if (home_ >= 0 && home_ != top_) {
        go = home_;
        home_ = -1;
      } else {
        home_ = -1;
      }
    }
    if (go >= 0 && go != top_) {
      top_ = go;
      nav.top = go;
      nav.rebuildNeeded = true;
    }
    walking_ = nav.rebuildNeeded;
  }

  // The page line: 1-based page and page count; false while a walk is still
  // building, or when the rows fit one page.
  bool position(int& page, int& pages) const {
    if (walking_ || count_ <= 0 || drawn_ <= 0 || (top_ == 0 && drawn_ >= count_)) return false;
    const int index = indexOf(top_);
    if (index >= 0 && complete_) {
      page = index + 1;
      pages = known_;
      return true;
    }
    // Estimated from the rows the remembered pages held.
    const int rows = known_ > 1 ? starts_[known_ - 1] / (known_ - 1) : drawn_;
    const int size = std::max(1, rows);
    pages = (count_ + size - 1) / size;
    page = std::min(pages, index >= 0 ? index + 1 : top_ / size + 1);
    if (top_ + drawn_ >= count_) page = pages;
    return true;
  }

  // Whether the band still lacks the line it needs (rows overflowed a band the
  // estimate said they fit): the caller builds once more.
  bool lineMissing() const { return lineWanted_ && !lineReserved_ && (top_ > 0 || top_ + drawn_ < count_); }

 private:
  void reset(const int count, const freeink::ui::ListNav* nav, const freeink::ui::Rect rows) {
    count_ = count;
    nav_ = nav;
    band_ = rows;
    starts_[0] = 0;
    known_ = 1;
    complete_ = false;
    offGrid_ = false;
    top_ = 0;
    drawn_ = 0;
    target_ = -1;
    home_ = -1;
    fresh_ = true;
  }

  // Remembers where the page after `page` starts; a page that now holds a
  // different number of rows moves every start after it.
  void record(const int page, const int next) {
    if (next >= count_) {
      known_ = page + 1;
      complete_ = true;
    } else if (page + 1 < known_) {
      if (starts_[page + 1] != next) {
        starts_[page + 1] = static_cast<uint16_t>(next);
        known_ = page + 2;
        complete_ = false;
      }
    } else if (known_ < kMaxPages) {
      starts_[known_++] = static_cast<uint16_t>(next);
    }
  }

  bool onPage(const int row) const { return drawn_ > 0 && row >= top_ && row < top_ + drawn_; }

  int indexOf(const int start) const {
    if (offGrid_) return -1;
    for (int i = 0; i < known_; ++i) {
      if (starts_[i] == start) return i;
    }
    return -1;
  }

  // The remembered page holding row, -1 when it lies past them.
  int pageOf(const int row) const {
    if (offGrid_) return -1;
    for (int i = 0; i < known_; ++i) {
      const bool last = i + 1 == known_;
      if (row >= starts_[i] && (last ? complete_ : row < starts_[i + 1])) return i;
    }
    return -1;
  }

  uint16_t starts_[kMaxPages] = {};
  int known_ = 0;
  bool complete_ = false;  // the last remembered page reaches the end
  bool offGrid_ = false;   // paging on from a row past kMaxPages
  bool lineWanted_ = false;
  bool lineReserved_ = false;
  bool walking_ = false;
  bool fresh_ = false;  // reset since the last follow
  int count_ = -1;
  const freeink::ui::ListNav* nav_ = nullptr;
  freeink::ui::Rect band_{};
  int top_ = 0;
  int drawn_ = 0;
  int target_ = -1;  // a selection past the remembered pages
  int home_ = -1;    // the page to come back to after counting pages
};
