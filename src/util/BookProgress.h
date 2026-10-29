#pragma once

#include <string>

struct BookPosition {
  int percent = -1;  // saved reading percentage, -1 when unavailable
  int chapter = 0;   // 1-based table-of-contents entry, 0 when unknown
  int chapterCount = 0;
  bool unreadable = false;  // saved progress exists but could not be read this time
};

// Saved reading position. Never builds reading caches.
BookPosition loadBookPosition(const std::string& path);

// Saved reading percentage, or -1 when unavailable. Never builds reading caches.
inline int loadBookProgress(const std::string& path) { return loadBookPosition(path).percent; }
