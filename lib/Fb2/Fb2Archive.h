#pragma once

// Adapted from guloff/folio-firmware (MIT)

#include <string>

// FB2 books on the SD card: `.fb2`, or `.fb2.zip` holding one.
namespace Fb2Archive {

// Transcodes the book into an EPUB at `epubPath` (written beside it, then
// renamed into place, so an interrupted run leaves nothing half-written).
bool convert(const std::string& fb2Path, const std::string& epubPath);

// The <coverpage> image, without converting: written to "<dir>/.cover.jpg" or
// ".cover.png" and its path returned, or "" when the book has no cover the
// reader can draw. One streaming pass that stops after the cover's <binary>.
std::string extractCover(const std::string& fb2Path, const std::string& dir);

// Title and authors (", "-joined) from <description>, without converting.
bool readMetadata(const std::string& fb2Path, std::string& title, std::string& author);

}  // namespace Fb2Archive
