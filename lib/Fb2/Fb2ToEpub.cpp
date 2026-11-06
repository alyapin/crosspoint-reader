// Adapted from guloff/folio-firmware (MIT)

#include "Fb2ToEpub.h"

#include <Memory.h>
#include <expat.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

// Single-byte encodings expat lacks, upper half (0x80-0xFF) as code points.
// Russian FB2 files are often windows-1251, older ones KOI8-R.
constexpr uint16_t CP1251_HIGH[128] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A,
    0x040C, 0x040B, 0x040F, 0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0xFFFD, 0x2122,
    0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F, 0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6,
    0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407, 0x00B0, 0x00B1, 0x0406, 0x0456,
    0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457, 0x0410,
    0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D,
    0x041E, 0x041F, 0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A,
    0x042B, 0x042C, 0x042D, 0x042E, 0x042F, 0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
    0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F, 0x0440, 0x0441, 0x0442, 0x0443, 0x0444,
    0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F};

constexpr uint16_t KOI8R_HIGH[128] = {
    0x2500, 0x2502, 0x250C, 0x2510, 0x2514, 0x2518, 0x251C, 0x2524, 0x252C, 0x2534, 0x253C, 0x2580, 0x2584,
    0x2588, 0x258C, 0x2590, 0x2591, 0x2592, 0x2593, 0x2320, 0x25A0, 0x2219, 0x221A, 0x2248, 0x2264, 0x2265,
    0x00A0, 0x2321, 0x00B0, 0x00B2, 0x00B7, 0x00F7, 0x2550, 0x2551, 0x2552, 0x0451, 0x2553, 0x2554, 0x2555,
    0x2556, 0x2557, 0x2558, 0x2559, 0x255A, 0x255B, 0x255C, 0x255D, 0x255E, 0x255F, 0x2560, 0x2561, 0x0401,
    0x2562, 0x2563, 0x2564, 0x2565, 0x2566, 0x2567, 0x2568, 0x2569, 0x256A, 0x256B, 0x256C, 0x00A9, 0x044E,
    0x0430, 0x0431, 0x0446, 0x0434, 0x0435, 0x0444, 0x0433, 0x0445, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C,
    0x043D, 0x043E, 0x043F, 0x044F, 0x0440, 0x0441, 0x0442, 0x0443, 0x0436, 0x0432, 0x044C, 0x044B, 0x0437,
    0x0448, 0x044D, 0x0449, 0x0447, 0x044A, 0x042E, 0x0410, 0x0411, 0x0426, 0x0414, 0x0415, 0x0424, 0x0413,
    0x0425, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F, 0x042F, 0x0420, 0x0421, 0x0422,
    0x0423, 0x0416, 0x0412, 0x042C, 0x042B, 0x0417, 0x0428, 0x042D, 0x0429, 0x0427, 0x042A};

// windows-1252 differs from ISO-8859-1 only in 0x80-0x9F.
constexpr uint16_t CP1252_C1[32] = {0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
                                    0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};

bool iequals(const char* a, const char* b) {
  for (; *a && *b; a++, b++) {
    char x = *a, y = *b;
    if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
    if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
    if (x != y) return false;
  }
  return *a == *b;
}

int XMLCALL unknownEncoding(void*, const XML_Char* name, XML_Encoding* info) {
  const uint16_t* high = nullptr;
  bool cp1252 = false;
  if (iequals(name, "windows-1251") || iequals(name, "cp1251")) {
    high = CP1251_HIGH;
  } else if (iequals(name, "koi8-r")) {
    high = KOI8R_HIGH;
  } else if (iequals(name, "windows-1252") || iequals(name, "cp1252")) {
    cp1252 = true;
  } else {
    return XML_STATUS_ERROR;
  }
  for (int i = 0; i < 128; i++) info->map[i] = i;
  for (int i = 128; i < 256; i++) {
    if (high) {
      info->map[i] = high[i - 128];
    } else {
      info->map[i] = (cp1252 && i < 0xA0) ? CP1252_C1[i - 0x80] : i;
    }
  }
  info->data = nullptr;
  info->convert = nullptr;
  info->release = nullptr;
  return XML_STATUS_OK;
}

const char* localName(const char* name) {
  const char* colon = strrchr(name, ':');
  return colon ? colon + 1 : name;
}

// Attribute by local name, so l:href, xlink:href and href all match.
const char* attribute(const XML_Char** atts, const char* name) {
  if (!atts) return nullptr;
  for (int i = 0; atts[i]; i += 2) {
    if (strcmp(localName(atts[i]), name) == 0) return atts[i + 1];
  }
  return nullptr;
}

// Attribute by its full name: FB2 marks notes with a plain type="note", while
// xlink:type="simple" on the same link must not hide it.
const char* exactAttribute(const XML_Char** atts, const char* name) {
  if (!atts) return nullptr;
  for (int i = 0; atts[i]; i += 2) {
    if (strcmp(atts[i], name) == 0) return atts[i + 1];
  }
  return nullptr;
}

const char* imageExtension(const char* contentType) {
  if (!contentType) return nullptr;
  if (iequals(contentType, "image/jpeg") || iequals(contentType, "image/jpg")) return "jpg";
  if (iequals(contentType, "image/png")) return "png";
  if (iequals(contentType, "image/gif")) return "gif";
  return nullptr;
}

const char* mediaType(const char* extension) {
  if (strcmp(extension, "jpg") == 0) return "image/jpeg";
  if (strcmp(extension, "png") == 0) return "image/png";
  return "image/gif";
}

void appendEscaped(std::string& out, const char* s, size_t len, bool attr = false) {
  for (size_t i = 0; i < len; i++) {
    switch (s[i]) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        if (attr) {
          out += "&quot;";
          break;
        }
        [[fallthrough]];
      default:
        out += s[i];
    }
  }
}

void appendEscaped(std::string& out, const std::string& s, bool attr = false) {
  appendEscaped(out, s.data(), s.size(), attr);
}

bool isXmlSpace(const char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

// One UTF-8 character; expat hands over whole ones.
uint32_t decodeUtf8(const char* s, const int n) {
  const auto b = [s](const int i) { return static_cast<uint32_t>(static_cast<uint8_t>(s[i])); };
  if (n == 2) return (b(0) & 0x1F) << 6 | (b(1) & 0x3F);
  if (n == 3) return (b(0) & 0x0F) << 12 | (b(1) & 0x3F) << 6 | (b(2) & 0x3F);
  if (n == 4) return (b(0) & 0x07) << 18 | (b(1) & 0x3F) << 12 | (b(2) & 0x3F) << 6 | (b(3) & 0x3F);
  return b(0);
}

int utf8Length(const char lead) {
  const auto c = static_cast<uint8_t>(lead);
  return c < 0xC0 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
}

bool isUpperLetter(const uint32_t c) { return (c >= 'A' && c <= 'Z') || (c >= 0x400 && c <= 0x42F); }
bool isLowerLetter(const uint32_t c) { return (c >= 'a' && c <= 'z') || (c >= 0x430 && c <= 0x45F); }
bool isTextSpace(const uint32_t c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == 0xA0; }
// ")", "»" and "”": a note marker may follow a closing quote or parenthesis.
bool isCloser(const uint32_t c) { return c == ')' || c == 0xBB || c == 0x201D; }
// What may follow a bare marker: a space, punctuation or a closing quote, not a letter or another digit.
bool endsMarker(const uint32_t c) {
  return isTextSpace(c) || isCloser(c) || c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?' ||
         c == 0x2026;
}

void collapseSpaces(std::string& s) {
  std::string out;
  out.reserve(s.size());
  bool space = false;
  for (const char c : s) {
    if (isXmlSpace(c)) {
      space = !out.empty();
    } else {
      if (space) out += ' ';
      space = false;
      out += c;
    }
  }
  s.swap(out);
}

// Appends while `s` is under `max` bytes. Expat hands over whole characters, so
// cutting before a lead byte keeps the UTF-8 valid.
void appendBounded(std::string& s, const char* data, size_t len, size_t max) {
  if (s.size() >= max) return;
  size_t n = std::min(len, max - s.size());
  while (n > 0 && n < len && (static_cast<uint8_t>(data[n]) & 0xC0) == 0x80) n--;
  s.append(data, n);
}

// Cuts `s` to at most `max` bytes without splitting a UTF-8 sequence.
void truncateUtf8(std::string& s, size_t max) {
  if (s.size() <= max) return;
  size_t n = max;
  while (n > 0 && (static_cast<uint8_t>(s[n]) & 0xC0) == 0x80) n--;
  s.resize(n);
}

bool idFits(const char* id) { return strlen(id) <= Fb2ToEpub::MAX_ID_BYTES; }

constexpr const char* XHTML_HEAD =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title></title>"
    "<link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\"/></head><body>";
constexpr const char* XHTML_TAIL = "</body></html>";
constexpr const char* SECTION_OPEN = "<div class=\"section\">";
// calibre writes runs of 20 <empty-line/>s for vertical layout; past this they only push text onto blank pages.
constexpr uint8_t MAX_EMPTY_LINES = 2;

constexpr const char* STYLE_CSS =
    "p{text-indent:1.5em;margin:0}"
    "h1,h2,h3,h4,h5,h6{text-align:center;text-indent:0}"
    // Insets go on the wrappers, so a text-author lines up under its epigraph or cite.
    "div.epigraph{margin-left:30%}"
    "p.epigraph{font-style:italic;text-indent:0}"
    "blockquote.cite{margin-left:1.5em;margin-right:1.5em}"
    "p.annotation{font-style:italic}"
    ".poem{margin-left:1.5em}"
    // The reader indents every first line unless its CSS indent is negative; -1px lines it up with the next verses.
    "p.stanza{text-indent:-1px}"
    ".v{text-indent:0}"
    ".text-author{text-align:right;font-style:italic;text-indent:0}"
    ".date{text-align:right;text-indent:0}"
    ".empty-line{text-indent:0}"
    ".note-title{font-weight:bold;text-indent:0}"
    ".image,.cover{text-align:center;text-indent:0}";

constexpr const char* CONTAINER_XML =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\"><rootfiles>"
    "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
    "</rootfiles></container>";

bool writeWhole(Fb2ToEpub::Output& out, const char* name, const char* data, size_t len) {
  return out.beginFile(name) && out.write(data, len) && out.endFile();
}

bool writeWhole(Fb2ToEpub::Output& out, const char* name, const std::string& data) {
  return writeWhole(out, name, data.data(), data.size());
}

// Streams one package file into the Output, which batches the small writes.
class PackageWriter {
 public:
  PackageWriter(Fb2ToEpub::Output& out, const char* name) : out(out), ok(out.beginFile(name)) {}
  void put(const char* s, size_t len) {
    if (ok && !out.write(s, len)) ok = false;
  }
  void put(const char* s) { put(s, strlen(s)); }
  void putEscaped(const std::string& s, bool attr = false) {
    buf.clear();
    appendEscaped(buf, s, attr);
    put(buf.data(), buf.size());
  }
  bool end() { return ok && out.endFile(); }

 private:
  Fb2ToEpub::Output& out;
  bool ok;
  std::string buf;
};

enum class Kind : uint8_t {
  Other,
  Description,
  TitleInfo,
  Author,
  CoverPage,
  Body,
  Section,
  Title,
  Line,       // a title's <p> or a stanza's <v>: text inside a paragraph its parent opened
  Wrapper,    // epigraph, cite, poem, ... : blocks splitting while open
  Stanza,     // a wrapper whose <v>s are the lines of one paragraph
  TextBlock,  // p, v, subtitle, td, ... : character data is content
  Inline,
  NoteLink,  // <a type="note">: its marker stays out of TOC labels
  Binary,
};

enum class Capture : uint8_t { None, Title, Language, AuthorPart };

struct Frame {
  Kind kind;
  const char* close;      // markup emitted at the end, or nullptr
  const char* paraClass;  // Wrapper: class for its plain paragraphs (the reader does not inherit div styles)
};

}  // namespace

struct Fb2ToEpub::Pass {
  Fb2ToEpub& self;
  Output* out;  // nullptr: measuring pass
  bool metadataOnly = false;

  XML_Parser parser = nullptr;
  bool sawRoot = false;
  bool notRoot = false;
  bool stopped = false;
  bool xmlError = false;
  bool outputError = false;

  std::vector<Frame> stack;
  uint16_t openKinds[static_cast<int>(Kind::Binary) + 1] = {};

  // Description.
  Capture capture = Capture::None;
  std::string captured;
  std::string authorParts[4];  // first, middle, last, nickname
  int authorPart = 0;

  // Bodies.
  bool notesBody = false;
  bool bodyContent = false;  // text or an image seen in a body
  uint8_t emptyLines = 0;    // <empty-line/>s since the last text or image
  uint8_t sectionDepth = 0;
  uint8_t sectionDivs = 0;  // section <div>s open in the output
  uint32_t sectionSerial = 0;
  int textDepth = 0;
  int titleLines = 0;
  bool verses = false;  // a stanza's paragraph is open

  // Note markers a converter (calibre) left as plain text: "часов1." or "скрининг[1]". Digits are
  // held back until what follows shows whether they are a marker, which is then raised.
  uint8_t letters = 0;       // letters right before the current position
  bool lowerLetter = false;  // ... one of them lowercase
  bool afterCloser = false;  // the last character closes a quote, parenthesis or superscript
  bool glued = false;        // the last character is not a space
  bool path = false;         // the current word has a '/', as a URL does
  bool heldBracket = false;  // a '[' is held
  char held[3];              // digits held
  uint8_t heldLen = 0;
  bool tocPending = false;
  uint8_t tocDepth = 0;
  std::string tocAnchor;
  std::string tocLabel;
  // The last TOC entry's anchor while only titles follow it in its file. A section opening
  // there takes the same anchor, so the reader keeps "Part" and "Chapter 1" on one page
  // instead of breaking before each.
  bool tocShareable = false;
  std::string lastTocAnchor;

  // Current content file.
  bool fileOpen = false;
  uint16_t fileNo = 0;
  size_t fileText = 0;          // character data only, so both passes split alike
  bool fileHasContent = false;  // text or an image, not just open section markup

  // Binary being decoded.
  bool decoding = false;
  uint32_t b64 = 0;
  int b64Bits = 0;

  std::string buf;

  Pass(Fb2ToEpub& self, Output* out) : self(self), out(out) {
    stack.reserve(32);
    buf.reserve(256);
  }

  bool measuring() const { return out == nullptr; }
  bool open(Kind k) const { return openKinds[static_cast<int>(k)] > 0; }

  void writeRaw(const char* s, size_t len) {
    if (out && !outputError && !out->write(s, len)) outputError = true;
  }
  void writeRaw(const char* s) { writeRaw(s, strlen(s)); }

  void openFile() {
    fileNo = ++self.fileCount;
    fileOpen = true;
    fileText = 0;
    fileHasContent = false;
    tocShareable = false;
    emptyLines = 0;  // a run before the break does not eat the new page's top spacing
    if (out) {
      char name[32];
      snprintf(name, sizeof(name), "OEBPS/s%u.xhtml", static_cast<unsigned>(fileNo));
      if (!out->beginFile(name)) outputError = true;
    }
    writeRaw(XHTML_HEAD);
    for (int i = 0; i < sectionDivs; i++) writeRaw(SECTION_OPEN);
  }

  void closeFile() {
    if (!fileOpen) return;
    for (int i = 0; i < sectionDivs; i++) writeRaw("</div>");
    writeRaw(XHTML_TAIL);
    if (out && !outputError && !out->endFile()) outputError = true;
    fileOpen = false;
  }

  void emit(const char* s, size_t len) {
    if (!fileOpen) openFile();
    writeRaw(s, len);
  }
  void emit(const char* s) { emit(s, strlen(s)); }
  void emit(const std::string& s) { emit(s.data(), s.size()); }

  bool wrapped() const { return open(Kind::Wrapper) || open(Kind::Stanza); }
  bool canSplit() const { return fileOpen && textDepth == 0 && !wrapped() && !open(Kind::Title); }

  void recordId(const char* id) {
    if (measuring() && id && *id && self.idFile.size() < MAX_IDS && idFits(id))
      self.idFile.emplace(id, fileOpen ? fileNo : static_cast<uint16_t>(self.fileCount + 1));
  }

  // "<tag class="cls" id="id">", recording the id against the file it lands in.
  void emitOpen(const char* tag, const char* cls, const char* id, const bool linkable = true) {
    buf = "<";
    buf += tag;
    if (cls) {
      buf += " class=\"";
      buf += cls;
      buf += '"';
    }
    if (id && *id) {
      buf += " id=\"";
      appendEscaped(buf, id, strlen(id), true);
      buf += '"';
    }
    buf += '>';
    if (!fileOpen) openFile();
    if (linkable) recordId(id);
    writeRaw(buf.data(), buf.size());
  }

  void push(Kind kind, const char* close = nullptr, const char* paraClass = nullptr) {
    stack.push_back({kind, close, paraClass});
    openKinds[static_cast<int>(kind)]++;
  }

  void startElement(const char* rawName, const XML_Char** atts) {
    endHeld();
    const char* name = localName(rawName);
    if (stack.size() >= MAX_DEPTH || self.fileCount >= MAX_FILES) {
      // Pushed because Expat still ends an empty element after a stop.
      push(Kind::Other);
      XML_StopParser(parser, XML_FALSE);
      return;
    }
    if (!sawRoot) {
      sawRoot = true;
      if (strcmp(name, "FictionBook") != 0) {
        notRoot = true;
        XML_StopParser(parser, XML_FALSE);
        return;
      }
      push(Kind::Other);
      return;
    }

    if (open(Kind::Description)) {
      startDescription(name, atts);
      return;
    }
    if (open(Kind::Binary)) {
      push(Kind::Other);
      return;
    }
    if (stack.size() == 1) {
      if (strcmp(name, "description") == 0) {
        push(Kind::Description);
      } else if (strcmp(name, "body") == 0) {
        startBody(atts);
      } else if (strcmp(name, "binary") == 0) {
        startBinary(atts);
      } else {
        push(Kind::Other);
      }
      return;
    }
    if (open(Kind::Body)) {
      startContent(name, atts);
      return;
    }
    push(Kind::Other);
  }

  void startDescription(const char* name, const XML_Char** atts) {
    if (strcmp(name, "title-info") == 0 && !open(Kind::TitleInfo)) {
      push(Kind::TitleInfo);
      return;
    }
    if (!open(Kind::TitleInfo) || !measuring()) {
      push(Kind::Other);
      return;
    }
    if (strcmp(name, "author") == 0) {
      for (auto& part : authorParts) part.clear();
      push(Kind::Author);
    } else if (strcmp(name, "coverpage") == 0) {
      push(Kind::CoverPage);
    } else if (open(Kind::CoverPage) && strcmp(name, "image") == 0) {
      const char* href = attribute(atts, "href");
      if (href && href[0] == '#' && self.meta.coverId.empty() && idFits(href + 1)) self.meta.coverId = href + 1;
      push(Kind::Other);
    } else if (open(Kind::Author)) {
      static constexpr const char* PARTS[] = {"first-name", "middle-name", "last-name", "nickname"};
      authorPart = -1;
      for (int i = 0; i < 4; i++) {
        if (strcmp(name, PARTS[i]) == 0) authorPart = i;
      }
      if (authorPart >= 0) beginCapture(Capture::AuthorPart);
      push(Kind::Other);
    } else if (strcmp(name, "book-title") == 0) {
      beginCapture(Capture::Title);
      push(Kind::Other);
    } else if (strcmp(name, "lang") == 0) {
      beginCapture(Capture::Language);
      push(Kind::Other);
    } else {
      push(Kind::Other);
    }
  }

  void beginCapture(const Capture c) {
    capture = c;
    captured.clear();
  }

  void endCapture() {
    collapseSpaces(captured);
    truncateUtf8(captured, MAX_METADATA_BYTES);
    switch (capture) {
      case Capture::Title:
        if (self.meta.title.empty()) self.meta.title = captured;
        break;
      case Capture::Language:
        if (self.meta.language.empty()) self.meta.language = captured;
        break;
      case Capture::AuthorPart:
        authorParts[authorPart] = captured;
        break;
      case Capture::None:
        break;
    }
    capture = Capture::None;
  }

  void endAuthor() {
    std::string author;
    for (int i = 0; i < 3; i++) {
      if (authorParts[i].empty()) continue;
      if (!author.empty()) author += ' ';
      author += authorParts[i];
    }
    if (author.empty()) author = authorParts[3];
    if (!author.empty() && self.meta.authors.size() < MAX_AUTHORS) self.meta.authors.push_back(std::move(author));
  }

  void startBody(const XML_Char** atts) {
    closeFile();
    const char* bodyName = attribute(atts, "name");
    notesBody = bodyName && *bodyName;
    sectionDepth = 0;
    sectionDivs = 0;
    textDepth = 0;
    // A notes body's title is its TOC entry; the main body's is the book title.
    tocPending = notesBody;
    tocDepth = 1;
    tocAnchor.clear();
    push(Kind::Body);
  }

  void startBinary(const XML_Char** atts) {
    closeFile();
    push(Kind::Binary);
    const char* id = attribute(atts, "id");
    if (!id || !*id) return;
    if (measuring()) {
      if (self.binaries.size() >= MAX_BINARIES || !idFits(id)) return;
      const Binary binary{static_cast<uint16_t>(self.binaries.size()), imageExtension(attribute(atts, "content-type"))};
      self.binaries.emplace(id, binary);
      return;
    }
    const auto it = self.binaries.find(id);
    if (it == self.binaries.end() || !it->second.extension) return;
    char name[40];
    snprintf(name, sizeof(name), "OEBPS/img%u.%s", static_cast<unsigned>(it->second.index), it->second.extension);
    if (!out->beginFile(name)) {
      outputError = true;
      return;
    }
    decoding = true;
    b64 = 0;
    b64Bits = 0;
  }

  void startSection(const char* id) {
    const size_t threshold = notesBody ? NOTES_SPLIT_BYTES : MAIN_SPLIT_BYTES;
    // A top-level section of the main body (chapter, part or front-matter page) opens a page of its own,
    // titled or not, however short the one before it, until the book has MAX_OWN_PAGE_FILES files.
    const bool ownPage = !notesBody && sectionDepth == 0 && fileHasContent && self.fileCount < MAX_OWN_PAGE_FILES;
    if (canSplit() && (ownPage || fileText >= threshold)) closeFile();
    const bool startsFile = !fileOpen;
    char synthetic[16];
    const bool named = id && *id;
    if (!named) {
      snprintf(synthetic, sizeof(synthetic), "fb2s%lu", static_cast<unsigned long>(++sectionSerial));
      id = synthetic;
    }
    // Only TOC entries point at a synthetic id, and they carry it themselves: no link-table entry.
    emitOpen("div", "section", id, named);
    sectionDivs++;
    sectionDepth++;
    push(Kind::Section, "</div>");
    if (!notesBody) {
      tocPending = true;
      tocDepth = sectionDepth;
      tocAnchor = tocShareable ? lastTocAnchor : (startsFile ? "" : id);
    }
  }

  void startTitle(const char* id) {
    char tag[4] = "h1";
    if (notesBody && sectionDepth > 0) {
      emitOpen("p", "note-title", id);
      push(Kind::Title, "</p>");
    } else {
      // A poem's or stanza's title sits a level below its section's. Past h4 the reader sets
      // headings smaller than the text, so deeper titles stay at h4.
      const int level = sectionDepth + 1 + (wrapped() ? 1 : 0);
      tag[1] = static_cast<char>('0' + (level > 4 ? 4 : level));
      emitOpen(tag, "title", id);
      static constexpr const char* CLOSE[] = {"</h1>", "</h2>", "</h3>", "</h4>"};
      push(Kind::Title, CLOSE[tag[1] - '1']);
    }
    titleLines = 0;
    tocLabel.clear();
  }

  void endTitle() {
    if (!tocPending) return;
    tocPending = false;
    collapseSpaces(tocLabel);
    if (!measuring() || tocLabel.empty() || self.toc.size() >= MAX_TOC_ENTRIES) return;
    truncateUtf8(tocLabel, MAX_LABEL_BYTES);
    self.toc.push_back({fileNo, tocDepth, tocAnchor, tocLabel});
    tocShareable = true;
    lastTocAnchor = tocAnchor;
  }

  void startImage(const XML_Char** atts) {
    // Open the file whether or not the image resolves, so both passes number files alike.
    if (!fileOpen) openFile();
    fileHasContent = true;
    tocShareable = false;
    push(Kind::Other);
    const char* href = attribute(atts, "href");
    const char* id = attribute(atts, "id");
    if (!href || href[0] != '#') return;
    // Converters such as calibre repeat the <coverpage> image as the body's first content.
    if (!bodyContent && !notesBody && !self.meta.coverId.empty() && self.meta.coverId == href + 1) {
      self.coverInBody = true;
    }
    bodyContent = true;
    emptyLines = 0;
    const auto it = self.binaries.find(href + 1);
    if (measuring() || it == self.binaries.end() || !it->second.extension) {
      recordId(id);
      return;
    }
    char src[96];
    snprintf(src, sizeof(src), "<img src=\"img%u.%s\" alt=\"\"/>", static_cast<unsigned>(it->second.index),
             it->second.extension);
    if (textDepth > 0) {
      emit(src);
    } else {
      emitOpen("div", "image", id);
      emit(src);
      emit("</div>");
    }
  }

  void startLink(const XML_Char** atts) {
    if (!fileOpen) openFile();
    const char* type = exactAttribute(atts, "type");
    const bool note = type && strcmp(type, "note") == 0;
    // Note markers are raised, as in print: at the baseline "word2" reads as one word.
    const bool raise = note && !inSup();
    const char* href = attribute(atts, "href");
    buf.clear();
    if (href && href[0] == '#') {
      const auto it = self.idFile.find(href + 1);
      if (it != self.idFile.end()) {
        char file[24];
        snprintf(file, sizeof(file), "s%u.xhtml", static_cast<unsigned>(it->second));
        buf = "<a href=\"";
        buf += file;
        appendEscaped(buf, href, strlen(href), true);
        buf += "\">";
      }
    } else if (href && *href) {
      buf = "<a href=\"";
      appendEscaped(buf, href, strlen(href), true);
      buf += "\">";
    }
    const bool link = !buf.empty();
    if (raise) buf += "<sup>";
    if (!buf.empty()) emit(buf);
    const char* close = link ? (raise ? "</sup></a>" : "</a>") : (raise ? "</sup>" : nullptr);
    push(note ? Kind::NoteLink : Kind::Inline, close);
  }

  bool inSup() const {
    return std::any_of(stack.begin(), stack.end(),
                       [](const Frame& f) { return f.close && strcmp(f.close, "</sup>") == 0; });
  }

  void startText(const char* tag, const char* cls, const char* id, const char* close) {
    tocPending = false;  // a section's title comes first; a later one belongs to a poem or stanza
    for (auto it = stack.rbegin(); !cls && it != stack.rend(); ++it) {
      if (it->kind == Kind::Wrapper) cls = it->paraClass;
    }
    emitOpen(tag, cls, id);
    resetMarkerScan();
    textDepth++;
    push(Kind::TextBlock, close);
  }

  void startWrapper(const char* tag, const char* cls, const char* id, const char* close,
                    const char* paraClass = nullptr) {
    tocPending = false;
    emitOpen(tag, cls, id);
    push(Kind::Wrapper, close, paraClass);
  }

  // A stanza's verses are the lines of one paragraph: as paragraphs of their own each would
  // get the reader's paragraph spacing, first-line indent and justification.
  void startVerse(const char* id) {
    if (verses) {
      emit("<br/>");
      if (id && *id) {
        // The reader keeps no anchor for an id on a <span>.
        emitOpen("a", nullptr, id);
        emit("</a>");
      }
    } else {
      emitOpen("p", "stanza", id);
      verses = true;
    }
    resetMarkerScan();
    textDepth++;
    push(Kind::Line);
  }

  void endVerses() {
    if (!verses) return;
    emit("</p>");
    verses = false;
  }

  void startInline(const char* tag, const char* close) {
    emit(tag);
    push(Kind::Inline, close);
  }

  void startContent(const char* name, const XML_Char** atts) {
    const char* id = attribute(atts, "id");
    const bool verse = strcmp(name, "v") == 0;
    if (verses && textDepth == 0 && !verse) endVerses();  // a title or subtitle among the verses
    if (open(Kind::Title)) {
      if (strcmp(name, "p") == 0) {
        if (titleLines++ > 0) {
          emit("<br/>");
          appendBounded(tocLabel, " ", 1, 2 * MAX_LABEL_BYTES);
        }
        recordId(id);
        textDepth++;
        push(Kind::Line);
        return;
      }
      if (strcmp(name, "empty-line") == 0) {
        push(Kind::Other);
        return;
      }
    }

    if (strcmp(name, "section") == 0) {
      startSection(id);
    } else if (strcmp(name, "title") == 0) {
      startTitle(id);
    } else if (strcmp(name, "p") == 0) {
      startText("p", nullptr, id, "</p>");
    } else if (verse) {
      if (!stack.empty() && stack.back().kind == Kind::Stanza) {
        startVerse(id);
      } else {
        startText("p", "v", id, "</p>");
      }
    } else if (strcmp(name, "subtitle") == 0) {
      // A heading, as the reader's paragraph alignment and indent override CSS on paragraphs: "* * *" stays centered.
      startText("h4", "subtitle", id, "</h4>");
    } else if (strcmp(name, "text-author") == 0) {
      startText("p", "text-author", id, "</p>");
    } else if (strcmp(name, "date") == 0) {
      startText("p", "date", id, "</p>");
    } else if (strcmp(name, "empty-line") == 0) {
      if (emptyLines < MAX_EMPTY_LINES) {
        emit("<p class=\"empty-line\">&#160;</p>");
        emptyLines++;
      }
      push(Kind::Other);
    } else if (strcmp(name, "epigraph") == 0) {
      startWrapper("div", "epigraph", id, "</div>", "epigraph");
    } else if (strcmp(name, "cite") == 0) {
      startWrapper("blockquote", "cite", id, "</blockquote>", "cite");
    } else if (strcmp(name, "poem") == 0) {
      startWrapper("div", "poem", id, "</div>");
    } else if (strcmp(name, "stanza") == 0) {
      if (textDepth > 0) {
        // Inside a verse or paragraph (malformed FB2) it cannot open a paragraph of its own.
        startWrapper("div", "stanza", id, "</div>");
      } else {
        tocPending = false;
        emitOpen("div", "stanza", id);
        push(Kind::Stanza, "</div>");
      }
    } else if (strcmp(name, "annotation") == 0) {
      startWrapper("div", "annotation", id, "</div>", "annotation");
    } else if (strcmp(name, "table") == 0) {
      startWrapper("table", nullptr, id, "</table>");
    } else if (strcmp(name, "tr") == 0) {
      startWrapper("tr", nullptr, id, "</tr>");
    } else if (strcmp(name, "td") == 0 || strcmp(name, "th") == 0) {
      startText(name, nullptr, id, name[1] == 'd' ? "</td>" : "</th>");
    } else if (strcmp(name, "image") == 0) {
      startImage(atts);
    } else if (strcmp(name, "a") == 0) {
      startLink(atts);
    } else if (strcmp(name, "emphasis") == 0) {
      startInline("<em>", "</em>");
    } else if (strcmp(name, "strong") == 0) {
      startInline("<strong>", "</strong>");
    } else if (strcmp(name, "strikethrough") == 0) {
      startInline("<s>", "</s>");
    } else if (strcmp(name, "sub") == 0) {
      startInline("<sub>", "</sub>");
    } else if (strcmp(name, "sup") == 0) {
      startInline("<sup>", "</sup>");
    } else if (strcmp(name, "code") == 0) {
      startInline("<code>", "</code>");
    } else {
      recordId(id);
      push(Kind::Other);
    }
  }

  void endElement() {
    endHeld();
    if (stack.empty()) return;
    const Frame frame = stack.back();
    stack.pop_back();
    openKinds[static_cast<int>(frame.kind)]--;
    // Digits glued after a superscript continue it: "человек[26]<sup>, </sup>1".
    if (frame.close && strcmp(frame.close, "</sup>") == 0) afterCloser = true;

    switch (frame.kind) {
      case Kind::Description:
        if (metadataOnly) {
          stopped = true;
          XML_StopParser(parser, XML_FALSE);
        }
        return;
      case Kind::Author:
        endAuthor();
        return;
      case Kind::Other:
        if (capture != Capture::None && open(Kind::Description)) endCapture();
        return;
      case Kind::Binary:
        endBinary();
        return;
      case Kind::Body:
        closeFile();
        sectionDivs = 0;
        return;
      case Kind::Section:
        sectionDepth--;
        sectionDivs--;
        // The file may have been split after this section's last paragraph.
        if (fileOpen) writeRaw(frame.close);
        return;
      case Kind::Line:
        textDepth--;
        return;
      case Kind::Stanza:
        endVerses();
        emit(frame.close);
        return;
      case Kind::Title:
        emit(frame.close);
        endTitle();
        return;
      case Kind::TextBlock:
        emit(frame.close);
        textDepth--;
        if (canSplit() && fileText >= MAX_FILE_BYTES) closeFile();
        return;
      default:
        if (frame.close) emit(frame.close);
        return;
    }
  }

  void endBinary() {
    if (!decoding) return;
    decoding = false;
    if (!outputError && !out->endFile()) outputError = true;
  }

  void decodeBase64(const char* s, const int len) {
    char chunk[128];
    size_t n = 0;
    for (int i = 0; i < len; i++) {
      const char c = s[i];
      int v;
      if (c >= 'A' && c <= 'Z') {
        v = c - 'A';
      } else if (c >= 'a' && c <= 'z') {
        v = c - 'a' + 26;
      } else if (c >= '0' && c <= '9') {
        v = c - '0' + 52;
      } else if (c == '+' || c == '-') {
        v = 62;
      } else if (c == '/' || c == '_') {
        v = 63;
      } else {
        continue;  // whitespace and '=' padding
      }
      b64 = (b64 << 6) | static_cast<uint32_t>(v);
      b64Bits += 6;
      if (b64Bits >= 8) {
        b64Bits -= 8;
        chunk[n++] = static_cast<char>((b64 >> b64Bits) & 0xFF);
        if (n == sizeof(chunk)) {
          writeRaw(chunk, n);
          n = 0;
        }
      }
    }
    if (n > 0) writeRaw(chunk, n);
  }

  void resetMarkerScan() {
    letters = 0;
    lowerLetter = afterCloser = glued = path = false;
  }

  // Digits a link, <sup>, <sub> or <code> already sets apart stay as they are.
  bool setApart() const {
    return std::any_of(stack.begin(), stack.end(), [](const Frame& f) {
      return f.close && (strstr(f.close, "</a>") || strstr(f.close, "</su") || strcmp(f.close, "</code>") == 0);
    });
  }

  void noteChar(const uint32_t c) {
    const bool lower = isLowerLetter(c);
    if (lower || isUpperLetter(c)) {
      if (letters < UINT8_MAX) letters++;
      lowerLetter = lowerLetter || lower;
    } else {
      letters = 0;
      lowerLetter = false;
    }
    afterCloser = isCloser(c);
    glued = !isTextSpace(c);
    if (!glued) {
      path = false;
    } else if (c == '/') {
      path = true;
    }
  }

  // Appends one character to buf, holding back digits that may be a note marker. A bare marker
  // follows a word of two letters or more (three when all capitals, so "СО2" stays) or a closing
  // quote, and ends at a space, punctuation or an element boundary; a bracketed one is glued to the
  // text before it. Numbers of four digits, such as years, never qualify.
  void scanMarker(const char* p, const int n, const uint32_t c, const bool detect) {
    const bool digit = c >= '0' && c <= '9';
    if (heldBracket || heldLen > 0) {
      if (digit && heldLen < sizeof(held)) {
        held[heldLen++] = static_cast<char>(c);
        return;
      }
      const bool closes = heldBracket && c == ']' && heldLen > 0;
      if (closes || (!heldBracket && !digit && endsMarker(c))) {
        raiseHeld();
      } else {
        releaseHeld();
      }
      if (closes) return;
    }
    if (detect && !path && digit && (afterCloser || (letters >= 2 && (lowerLetter || letters >= 3)))) {
      held[0] = static_cast<char>(c);
      heldLen = 1;
      return;
    }
    if (detect && !path && c == '[' && glued) {
      heldBracket = true;
      return;
    }
    appendEscaped(buf, p, static_cast<size_t>(n));
    noteChar(c);
  }

  void raiseHeld() {
    buf += "<sup>";
    if (heldBracket) buf += '[';
    buf.append(held, heldLen);
    if (heldBracket) buf += ']';
    buf += "</sup>";
    heldBracket = false;
    heldLen = 0;
    resetMarkerScan();
    glued = true;
  }

  void releaseHeld() {
    if (heldBracket) {
      buf += '[';
      noteChar('[');
      heldBracket = false;
    }
    for (uint8_t i = 0; i < heldLen; i++) {
      buf += held[i];
      noteChar(static_cast<uint8_t>(held[i]));
    }
    heldLen = 0;
  }

  // An element starts or ends: a bare marker held so far is complete, a bracketed one is not.
  void endHeld() {
    if (!heldBracket && heldLen == 0) return;
    buf.clear();
    if (heldBracket) {
      releaseHeld();
    } else {
      raiseHeld();
    }
    emit(buf);
  }

  void characters(const XML_Char* s, const int len) {
    if (capture != Capture::None) {
      appendBounded(captured, s, static_cast<size_t>(len), 2 * MAX_METADATA_BYTES);
      return;
    }
    if (decoding) {
      decodeBase64(s, len);
      return;
    }
    if (textDepth <= 0 || !open(Kind::Body)) return;
    buf.clear();
    const bool detect = !open(Kind::Title) && !setApart();
    for (int i = 0; i < len;) {
      const int n = std::min(utf8Length(s[i]), len - i);
      scanMarker(s + i, n, decodeUtf8(s + i, n), detect);
      i += n;
    }
    emit(buf);
    fileText += static_cast<size_t>(len);
    if (std::any_of(s, s + len, [](const char c) { return !isXmlSpace(c); })) {
      fileHasContent = true;
      bodyContent = true;
      emptyLines = 0;
    }
    if (!open(Kind::Title)) tocShareable = false;
    if (open(Kind::Title) && tocPending && !open(Kind::NoteLink)) {
      appendBounded(tocLabel, s, static_cast<size_t>(len), 2 * MAX_LABEL_BYTES);
    }
  }

  static void XMLCALL onStart(void* ud, const XML_Char* name, const XML_Char** atts) {
    static_cast<Pass*>(ud)->startElement(name, atts);
  }
  static void XMLCALL onEnd(void* ud, const XML_Char*) { static_cast<Pass*>(ud)->endElement(); }
  static void XMLCALL onText(void* ud, const XML_Char* s, int len) { static_cast<Pass*>(ud)->characters(s, len); }

  static bool feed(void* ctx, const char* data, size_t len) {
    auto* pass = static_cast<Pass*>(ctx);
    if (pass->stopped || pass->xmlError || pass->notRoot || pass->outputError) return false;
    const XML_Status status = XML_Parse(pass->parser, data, static_cast<int>(len), XML_FALSE);
    if (status == XML_STATUS_ERROR) {
      if (!pass->stopped && !pass->notRoot) pass->xmlError = true;
      return false;
    }
    return !pass->stopped && !pass->notRoot && !pass->outputError;
  }

  // Runs the document through this pass. False only when it could not be read.
  bool run(Input& input) {
    parser = XML_ParserCreate(nullptr);
    if (!parser) return false;
    XML_SetUserData(parser, this);
    XML_SetElementHandler(parser, onStart, onEnd);
    XML_SetCharacterDataHandler(parser, onText);
    XML_SetUnknownEncodingHandler(parser, unknownEncoding, nullptr);
    const bool streamed = input.stream(feed, this);
    if (streamed && !stopped && !xmlError && !notRoot && !outputError) {
      if (XML_Parse(parser, nullptr, 0, XML_TRUE) == XML_STATUS_ERROR) xmlError = true;
    }
    XML_ParserFree(parser);
    parser = nullptr;
    // A book cut short by an XML error keeps what came before it: close
    // whatever was open so every written file is well-formed.
    if (xmlError && !metadataOnly) {
      while (!stack.empty()) endElement();
    }
    return streamed;
  }
};

bool Fb2ToEpub::readMetadata(Input& input, Metadata& out) {
  Fb2ToEpub converter;
  // Passes hold several strings and the element stack: heap, not the reader task's stack.
  auto pass = makeUniqueNoThrow<Pass>(converter, nullptr);
  if (!pass) return false;
  pass->metadataOnly = true;
  if (!pass->run(input) || pass->xmlError || pass->notRoot || !pass->sawRoot) return false;
  out = std::move(converter.meta);
  return true;
}

bool Fb2ToEpub::convert(Input& input, Output& out) {
  meta = Metadata{};
  idFile.clear();
  binaries.clear();
  toc.clear();
  cover = nullptr;
  coverInBody = false;

  {
    fileCount = 0;
    auto measure = makeUniqueNoThrow<Pass>(*this, nullptr);
    if (!measure || !measure->run(input) || measure->notRoot || !measure->sawRoot || fileCount == 0) return false;
  }
  const uint16_t measuredFiles = fileCount;
  if (!meta.coverId.empty()) {
    const auto it = binaries.find(meta.coverId);
    if (it != binaries.end() && it->second.extension) cover = &it->second;
  }

  if (!writeWhole(out, "mimetype", "application/epub+zip", 20) ||
      !writeWhole(out, "META-INF/container.xml", CONTAINER_XML, strlen(CONTAINER_XML)) ||
      !writeWhole(out, "OEBPS/style.css", STYLE_CSS, strlen(STYLE_CSS))) {
    return false;
  }
  if (coverPage()) {
    std::string page = XHTML_HEAD;
    char img[64];
    snprintf(img, sizeof(img), "<div class=\"cover\"><img src=\"img%u.%s\" alt=\"\"/></div>",
             static_cast<unsigned>(cover->index), cover->extension);
    page += img;
    page += XHTML_TAIL;
    if (!writeWhole(out, "OEBPS/cover.xhtml", page)) return false;
  }

  fileCount = 0;
  auto write = makeUniqueNoThrow<Pass>(*this, &out);
  if (!write || !write->run(input) || write->outputError || fileCount != measuredFiles) return false;

  return writeOpf(out) && writeNcx(out);
}

bool Fb2ToEpub::writeOpf(Output& out) const {
  PackageWriter w(out, "OEBPS/content.opf");
  w.put(
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"2.0\" unique-identifier=\"uid\">"
      "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:opf=\"http://www.idpf.org/2007/opf\">"
      "<dc:title>");
  w.putEscaped(meta.title);
  w.put("</dc:title>");
  for (const auto& author : meta.authors) {
    w.put("<dc:creator opf:role=\"aut\">");
    w.putEscaped(author);
    w.put("</dc:creator>");
  }
  w.put("<dc:language>");
  w.putEscaped(meta.language.empty() ? std::string("und") : meta.language);
  w.put("</dc:language><dc:identifier id=\"uid\">fb2</dc:identifier>");
  char line[160];
  if (cover) {
    snprintf(line, sizeof(line), "<meta name=\"cover\" content=\"img%u\"/>", static_cast<unsigned>(cover->index));
    w.put(line);
  }
  w.put(
      "</metadata><manifest>"
      "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>"
      "<item id=\"css\" href=\"style.css\" media-type=\"text/css\"/>");
  if (coverPage()) {
    w.put("<item id=\"cover\" href=\"cover.xhtml\" media-type=\"application/xhtml+xml\"/>");
  }
  for (unsigned i = 1; i <= fileCount; i++) {
    snprintf(line, sizeof(line), "<item id=\"s%u\" href=\"s%u.xhtml\" media-type=\"application/xhtml+xml\"/>", i, i);
    w.put(line);
  }
  for (const auto& entry : binaries) {
    const Binary& b = entry.second;
    if (!b.extension) continue;
    snprintf(line, sizeof(line), "<item id=\"img%u\" href=\"img%u.%s\" media-type=\"%s\"/>",
             static_cast<unsigned>(b.index), static_cast<unsigned>(b.index), b.extension, mediaType(b.extension));
    w.put(line);
  }
  w.put("</manifest><spine toc=\"ncx\">");
  if (coverPage()) w.put("<itemref idref=\"cover\"/>");
  for (unsigned i = 1; i <= fileCount; i++) {
    snprintf(line, sizeof(line), "<itemref idref=\"s%u\"/>", i);
    w.put(line);
  }
  w.put("</spine></package>");
  return w.end();
}

bool Fb2ToEpub::writeNcx(Output& out) const {
  PackageWriter w(out, "OEBPS/toc.ncx");
  w.put(
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\">"
      "<head><meta name=\"dtb:uid\" content=\"fb2\"/></head><docTitle><text>");
  w.putEscaped(meta.title);
  w.put("</text></docTitle><navMap>");

  int openDepth = 0;
  unsigned order = 0;
  char line[96];
  const auto navPoint = [&](const uint16_t file, const uint8_t entryDepth, const std::string& anchor,
                            const std::string& label) {
    // A level with no titled parent hangs off the nearest open one.
    int depth = entryDepth < 1 ? 1 : entryDepth;
    if (depth > openDepth + 1) depth = openDepth + 1;
    for (; openDepth >= depth; openDepth--) w.put("</navPoint>");
    order++;
    snprintf(line, sizeof(line), "<navPoint id=\"n%u\" playOrder=\"%u\"><navLabel><text>", order, order);
    w.put(line);
    w.putEscaped(label);
    if (file == 0) {
      w.put("</text></navLabel><content src=\"cover.xhtml");
    } else {
      snprintf(line, sizeof(line), "</text></navLabel><content src=\"s%u.xhtml", static_cast<unsigned>(file));
      w.put(line);
    }
    if (!anchor.empty()) {
      w.put("#");
      w.putEscaped(anchor, true);
    }
    w.put("\"/>");
    openDepth = depth;
  };

  // File 0 is the cover page, titled with the book so the status bar has a name for it.
  const std::string noAnchor;
  const std::string bookLabel = meta.title.empty() ? std::string("1") : meta.title;
  if (coverPage()) navPoint(0, 1, noAnchor, bookLabel);
  if (toc.empty()) navPoint(1, 1, noAnchor, bookLabel);
  for (const TocEntry& e : toc) navPoint(e.file, e.depth, e.anchor, e.label);
  for (; openDepth > 0; openDepth--) w.put("</navPoint>");
  w.put("</navMap></ncx>");
  return w.end();
}
