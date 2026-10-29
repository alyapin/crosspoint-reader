#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

// Flashcard decks: text files in DECK_DIR, one card per line. Accepted: Anki's
// "Notes in Plain Text" export with its #-headers, .tsv, and .csv with tab, ;
// or , between fields (guessed in that order); CSV quoting, UTF-8 or CP1251.
// Fields are cleaned while they stream in, without a raw line buffer: HTML tags
// dropped (<br>, <div>, <p>, <li> become line breaks), entities decoded,
// images and [sound:] removed, whitespace collapsed, FIELD_MAX bytes kept.
// Field 1 is the front, 2 the answer, 3 an example shown under it.
namespace cards {

inline constexpr char DECK_DIR[] = "/Карточки";
inline constexpr size_t FIELD_MAX = 1024;
inline constexpr size_t FIELD_CAP = FIELD_MAX + 4;  // room for the "…"
inline constexpr int FIELDS = 3;
inline constexpr uint16_t MAX_CARDS = 2000;
inline constexpr uint32_t MAX_DECK_BYTES = 4u << 20;
inline constexpr size_t PROBE_BYTES = 4096;

bool isDeckName(std::string_view name);   // .txt, .tsv, .csv
bool isTableName(std::string_view name);  // .tsv, .csv: never a book
bool isCsvName(std::string_view name);
// A .txt is a deck when it opens with an Anki export header.
bool hasAnkiHeader(const uint8_t* head, size_t len);
uint32_t fnv1a(const void* data, size_t len, uint32_t hash = 2166136261u);

struct Format {
  char separator = '\t';
  bool html = true;
  bool cp1251 = false;
  int8_t guidColumn = -1;
  uint16_t hiddenColumns = 0;  // a bit per column: guid, notetype, deck, tags
  uint32_t dataStart = 0;
};

// The format read from the first PROBE_BYTES of a deck; csv guesses the separator.
Format probe(const uint8_t* head, size_t len, bool csv);

enum Media : uint8_t { NoMedia = 0, HasImage = 1, HasSound = 2 };

struct Fields {
  char text[FIELDS][FIELD_CAP];
  uint8_t media[FIELDS];
};

class ByteSource {
 public:
  // Bytes read; 0 at the end, negative on an error.
  virtual int read(uint8_t* buf, size_t cap) = 0;

 protected:
  ~ByteSource() = default;
};

// Streams records from `offset` (a record start) of a source positioned there.
class RecordReader {
 public:
  RecordReader(ByteSource& source, const Format& format, uint32_t offset);
  // The next record that has any text or media; false at the end or on a read
  // error (failed()). `offset` is where the record starts.
  bool next(Fields& out, uint32_t& offset, uint32_t& guidHash);
  bool failed() const { return error; }

 private:
  int get();
  int peek();
  void beginColumn(Fields& out);
  void put(uint8_t b);
  void emit(uint8_t b);
  void emitCodepoint(uint32_t cp);
  void append(uint8_t b);
  void endTag();
  void endEntity();
  void endColumn();

  ByteSource& source;
  const Format format;
  uint32_t pos;
  uint8_t buf[256];
  uint16_t bufLen = 0;
  uint16_t bufPos = 0;
  bool error = false;

  Fields* fields = nullptr;
  int column = 0;
  int target = -1;  // field index, -1 dropped, -2 the guid
  uint32_t guid = 0;
  char* text = nullptr;
  size_t len = 0;
  bool full = false;
  bool pendingSpace = false;
  bool pendingBreak = false;
  bool pendingLt = false;
  bool inTag = false;
  bool tagNameOpen = false;
  bool inEntity = false;
  uint8_t tagLen = 0;
  uint8_t entityLen = 0;
  char tag[8];
  char entity[12];
};

// The card's identity: the guid when the export has one, else its front.
uint32_t cardKey(const Fields& fields, bool hasGuid, uint32_t guidHash);
inline bool hasFront(const Fields& fields) { return fields.text[0][0] != '\0' || fields.media[0] != NoMedia; }

}  // namespace cards
