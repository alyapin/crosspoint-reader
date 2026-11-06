// Adapted from guloff/folio-firmware (MIT)

#include <MinizConfig.h>
#include <expat.h>
#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "Fb2ToEpub.h"
#include "StoredZipWriter.h"

namespace {

// Feeds the document in small chunks so tags, UTF-8 and base64 straddle them.
class StringInput : public Fb2ToEpub::Input {
 public:
  explicit StringInput(std::string doc, size_t chunk = 7) : doc(std::move(doc)), chunk(chunk) {}
  bool stream(Fb2ToEpub::Sink sink, void* ctx) override {
    streams++;
    for (size_t i = 0; i < doc.size(); i += chunk) {
      if (!sink(ctx, doc.data() + i, std::min(chunk, doc.size() - i))) return true;
      consumed = i + chunk;
    }
    return true;
  }
  std::string doc;
  size_t chunk;
  int streams = 0;
  size_t consumed = 0;
};

class MapOutput : public Fb2ToEpub::Output {
 public:
  bool beginFile(const char* name) override {
    if (open) return false;
    open = true;
    order.emplace_back(name);
    files[name].clear();
    return true;
  }
  bool write(const char* data, size_t len) override {
    if (!open) return false;
    files[order.back()].append(data, len);
    return true;
  }
  bool endFile() override {
    const bool was = open;
    open = false;
    return was;
  }
  std::vector<std::string> order;
  std::map<std::string, std::string> files;
  bool open = false;
};

bool wellFormed(const std::string& xml) {
  XML_Parser p = XML_ParserCreate(nullptr);
  const bool ok = XML_Parse(p, xml.data(), static_cast<int>(xml.size()), XML_TRUE) == XML_STATUS_OK;
  XML_ParserFree(p);
  return ok;
}

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

std::string repeat(const std::string& s, int n) {
  std::string out;
  for (int i = 0; i < n; i++) out += s;
  return out;
}

const std::string COVER_BYTES = std::string("\xFF\xD8\xFF\xE0", 4) + "folio-cover";
const std::string COVER_BASE64 = "/9j/4GZvbGlv\nLWNvdmVy";  // COVER_BYTES, wrapped like real FB2

std::string sampleBook(const std::string& chapterOneText) {
  return std::string(
             "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
             "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\" "
             "xmlns:l=\"http://www.w3.org/1999/xlink\">"
             "<description><title-info><genre>prose</genre>"
             "<author><first-name>Лев</first-name><middle-name>Николаевич</middle-name>"
             "<last-name>Толстой</last-name></author>"
             "<author><nickname>Соавтор</nickname></author>"
             "<book-title>Тестовая  книга</book-title>"
             "<annotation><p>Аннотация</p></annotation>"
             "<coverpage><image l:href=\"#cover.jpg\"/></coverpage><lang>ru</lang></title-info>"
             "<document-info><author><nickname>верстальщик</nickname></author></document-info>"
             "</description>"
             "<body><title><p>Лев Толстой</p><p>Тестовая книга</p></title>"
             "<epigraph><p>Эпиграф &amp; цитата</p><text-author>Кто-то</text-author></epigraph>"
             "<section><title><p>Часть первая</p></title>"
             "<section><title><p>Глава 1</p></title>"
             "<p>Начало <emphasis>курсив</emphasis> и <strong>жирный</strong> текст"
             "<a l:href=\"#n1\" type=\"note\">[1]</a>.</p><p>") +
         chapterOneText +
         "</p></section>"
         "<section><title><p>Глава 2</p></title>"
         "<poem><title><p>Стих</p></title><stanza><v>Строка один</v><v>Строка два</v></stanza></poem>"
         "<empty-line/><p>Ссылка <a l:href=\"http://example.com/?a=1&amp;b=2\">наружу</a></p>"
         "</section></section></body>"
         "<body name=\"notes\"><title><p>Примечания</p></title>"
         "<section id=\"n1\"><title><p>1</p></title><p>Текст сноски</p></section></body>"
         "<binary id=\"cover.jpg\" content-type=\"image/jpeg\">" +
         COVER_BASE64 +
         "</binary>"
         "<binary id=\"unused.bmp\" content-type=\"image/bmp\">AAAA</binary>"
         "</FictionBook>";
}

const std::string LONG_TEXT = repeat("Длинный текст главы. ", 120);

TEST(Fb2ToEpub, ReadsMetadataFromTitleInfoOnly) {
  StringInput input(sampleBook("x"));
  Fb2ToEpub::Metadata meta;
  ASSERT_TRUE(Fb2ToEpub::readMetadata(input, meta));
  EXPECT_EQ(meta.title, "Тестовая книга");
  ASSERT_EQ(meta.authors.size(), 2u);
  EXPECT_EQ(meta.authors[0], "Лев Николаевич Толстой");
  EXPECT_EQ(meta.authors[1], "Соавтор");
  EXPECT_EQ(meta.language, "ru");
  EXPECT_EQ(meta.coverId, "cover.jpg");
  // Stops after </description>, well before the body.
  EXPECT_LT(input.consumed, input.doc.find("<body>") + 16);
}

TEST(Fb2ToEpub, MetadataNeedsOnlyTheDescription) {
  StringInput input(
      "<FictionBook><description><title-info><book-title>Т</book-title></title-info></description><body><p>");
  Fb2ToEpub::Metadata meta;
  ASSERT_TRUE(Fb2ToEpub::readMetadata(input, meta));
  EXPECT_EQ(meta.title, "Т");
}

TEST(Fb2ToEpub, RejectsOtherXml) {
  StringInput input("<html><body><p>hi</p></body></html>");
  MapOutput out;
  Fb2ToEpub converter;
  EXPECT_FALSE(converter.convert(input, out));
  Fb2ToEpub::Metadata meta;
  EXPECT_FALSE(Fb2ToEpub::readMetadata(input, meta));
}

TEST(Fb2ToEpub, WritesPackageInOrder) {
  StringInput input(sampleBook(LONG_TEXT));
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  EXPECT_EQ(input.streams, 2);

  const std::vector<std::string> expected = {"mimetype",          "META-INF/container.xml", "OEBPS/style.css",
                                             "OEBPS/cover.xhtml", "OEBPS/s1.xhtml",         "OEBPS/s2.xhtml",
                                             "OEBPS/s3.xhtml",    "OEBPS/s4.xhtml",         "OEBPS/img0.jpg",
                                             "OEBPS/content.opf", "OEBPS/toc.ncx"};
  EXPECT_EQ(out.order, expected);
  EXPECT_EQ(out.files["mimetype"], "application/epub+zip");
  for (const auto& [name, data] : out.files) {
    if (name.ends_with(".xhtml") || name.ends_with(".opf") || name.ends_with(".ncx") || name.ends_with(".xml")) {
      EXPECT_TRUE(wellFormed(data)) << name << "\n" << data;
    }
  }
}

TEST(Fb2ToEpub, MapsContentMarkup) {
  StringInput input(sampleBook(LONG_TEXT));
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  const std::string& s1 = out.files["OEBPS/s1.xhtml"];
  EXPECT_TRUE(contains(s1, "<h1 class=\"title\">Лев Толстой<br/>Тестовая книга</h1>"));
  EXPECT_TRUE(contains(
      s1, "<div class=\"epigraph\"><p class=\"epigraph\">Эпиграф &amp; цитата</p><p class=\"text-author\">Кто-то</p>"));
  // The epigraph's inset holds for its author line too.
  EXPECT_TRUE(contains(out.files["OEBPS/style.css"], "div.epigraph{margin-left:30%}"));
  EXPECT_FALSE(contains(s1, "Аннотация"));

  // The top-level section opens a file of its own after the body title.
  const std::string& s2 = out.files["OEBPS/s2.xhtml"];
  EXPECT_TRUE(contains(s2, "<body><div class=\"section\" id=\"fb2s1\"><h2 class=\"title\">Часть первая</h2>"));
  EXPECT_TRUE(contains(s2, "<h3 class=\"title\">Глава 1</h3>"));
  EXPECT_TRUE(contains(s2, "<em>курсив</em> и <strong>жирный</strong>"));

  // Глава 2 opens a file of its own, re-opening the enclosing section.
  const std::string& s3 = out.files["OEBPS/s3.xhtml"];
  EXPECT_TRUE(contains(s3, "<body><div class=\"section\"><div class=\"section\" id=\"fb2s3\"><h3"));
  EXPECT_TRUE(contains(s3,
                       "<div class=\"poem\"><h4 class=\"title\">Стих</h4><div class=\"stanza\"><p class=\"stanza\">"
                       "Строка один<br/>Строка два</p></div></div>"));
  EXPECT_TRUE(contains(s3, "<p class=\"empty-line\">"));
  EXPECT_TRUE(contains(s3, "<a href=\"http://example.com/?a=1&amp;b=2\">наружу</a>"));
}

TEST(Fb2ToEpub, ResolvesNoteLinksIntoTheNotesFile) {
  StringInput input(sampleBook(LONG_TEXT));
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  EXPECT_TRUE(contains(out.files["OEBPS/s2.xhtml"], "<a href=\"s4.xhtml#n1\"><sup>[1]</sup></a>"));

  const std::string& notes = out.files["OEBPS/s4.xhtml"];
  EXPECT_TRUE(contains(notes, "<h1 class=\"title\">Примечания</h1>"));
  EXPECT_TRUE(contains(notes, "<div class=\"section\" id=\"n1\"><p class=\"note-title\">1</p><p>Текст сноски</p>"));
}

TEST(Fb2ToEpub, RaisesNoteMarkers) {
  StringInput input(
      "<FictionBook xmlns:l=\"x\"><body><section><p>слово<a l:href=\"#n1\" l:type=\"simple\" type=\"note\">2</a>, "
      "<sup><a l:href=\"#n1\" type=\"note\">3</a></sup>, <a l:href=\"#nx\" type=\"note\">4</a>, "
      "<a l:href=\"#n1\">ссылка</a></p></section></body><body name=\"notes\"><section id=\"n1\"><p>сноска</p>"
      "</section></body></FictionBook>");
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  // Already raised notes are not raised twice; a note with no target is raised without a link.
  EXPECT_TRUE(contains(out.files["OEBPS/s1.xhtml"],
                       "слово<a href=\"s2.xhtml#n1\"><sup>2</sup></a>, <sup><a href=\"s2.xhtml#n1\">3</a></sup>, "
                       "<sup>4</sup>, <a href=\"s2.xhtml#n1\">ссылка</a></p>"));
}

TEST(Fb2ToEpub, RaisesNoteMarkersLeftAsPlainText) {
  // calibre's FB2 of «Всё хорошо!» keeps source references as bare digits after the last word and
  // editor's notes as glued "[N]", with no links or ids.
  const std::string doc =
      "<FictionBook xmlns:l=\"x\"><body><section>"
      "<p>на пару часов1. По данным СВДС20, MMR7 и LactMed31. «Спокойная и уверенная»1, в сна)15.</p>"
      "<p>Проведут скрининг[1]. В возрасте[8] — и <emphasis>слово</emphasis>12. Конец взрослым?»20</p>"
      "<p>заболевших 86 человек[26]<sup>, </sup>1.</p>"
      "<p><strong>[1]</strong> Неонатальный скрининг. <strong>2.</strong> Список</p>"
      "<p>В 2013 году, 11,4 или 16,1; СО2. Витамин К2. IQ5. Р. e702–е770. Года2013. 1990-х. Глава 12.</p>"
      "<p>H<sub>2</sub>O, 20<sup>th</sup>, <a l:href=\"http://x.org/r1\">www.region10.org</a>, "
      "<code>a[1]</code>, http://www.region10.org/r1 и [3] отдельно.</p>"
      "</section></body></FictionBook>";
  for (const size_t chunk : {size_t{1}, size_t{7}, doc.size()}) {
    StringInput input(doc, chunk);
    MapOutput out;
    Fb2ToEpub converter;
    ASSERT_TRUE(converter.convert(input, out));
    const std::string& s1 = out.files["OEBPS/s1.xhtml"];
    EXPECT_TRUE(contains(s1,
                         "<p>на пару часов<sup>1</sup>. По данным СВДС<sup>20</sup>, MMR<sup>7</sup> и "
                         "LactMed<sup>31</sup>. «Спокойная и уверенная»<sup>1</sup>, в сна)<sup>15</sup>.</p>"))
        << chunk << "\n"
        << s1;
    EXPECT_TRUE(contains(s1,
                         "<p>Проведут скрининг<sup>[1]</sup>. В возрасте<sup>[8]</sup> — и <em>слово</em>"
                         "<sup>12</sup>. Конец взрослым?»<sup>20</sup></p>"))
        << chunk;
    EXPECT_TRUE(contains(s1, "<p>заболевших 86 человек<sup>[26]</sup><sup>, </sup><sup>1</sup>.</p>")) << chunk;
    // Labels opening a note or a list item, real numbers and digits already set apart stay as they are.
    EXPECT_TRUE(contains(s1, "<p><strong>[1]</strong> Неонатальный скрининг. <strong>2.</strong> Список</p>"));
    EXPECT_TRUE(contains(
        s1, "<p>В 2013 году, 11,4 или 16,1; СО2. Витамин К2. IQ5. Р. e702–е770. Года2013. 1990-х. Глава 12.</p>"));
    EXPECT_TRUE(contains(s1,
                         "<p>H<sub>2</sub>O, 20<sup>th</sup>, <a href=\"http://x.org/r1\">www.region10.org</a>, "
                         "<code>a[1]</code>, http://www.region10.org/r1 и [3] отдельно.</p>"))
        << chunk;
    EXPECT_TRUE(wellFormed(s1));
  }
}

TEST(Fb2ToEpub, BuildsNestedTocWithAnchors) {
  StringInput input(sampleBook(LONG_TEXT));
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  const std::string& ncx = out.files["OEBPS/toc.ncx"];
  EXPECT_TRUE(contains(ncx,
                       "<navMap><navPoint id=\"n1\" playOrder=\"1\"><navLabel><text>Тестовая книга</text></navLabel>"
                       "<content src=\"cover.xhtml\"/></navPoint>"
                       "<navPoint id=\"n2\" playOrder=\"2\"><navLabel><text>Часть первая</text></navLabel>"
                       "<content src=\"s2.xhtml\"/>"
                       "<navPoint id=\"n3\" playOrder=\"3\"><navLabel><text>Глава 1</text></navLabel>"
                       "<content src=\"s2.xhtml\"/></navPoint>"
                       "<navPoint id=\"n4\" playOrder=\"4\"><navLabel><text>Глава 2</text></navLabel>"
                       "<content src=\"s3.xhtml\"/></navPoint></navPoint>"
                       "<navPoint id=\"n5\" playOrder=\"5\"><navLabel><text>Примечания</text></navLabel>"
                       "<content src=\"s4.xhtml\"/></navPoint></navMap>"))
      << ncx;
  // The poem's title is not a chapter.
  EXPECT_FALSE(contains(ncx, "Стих"));
}

TEST(Fb2ToEpub, KeepsConsecutiveTitlesOnOnePage) {
  // The reader breaks the page before every TOC anchor: a chapter opening right under its part's
  // title shares the part's anchor, while a chapter after text keeps its own.
  StringInput input(
      "<FictionBook><body><section><p>до</p></section><section id=\"part\"><title><p>Часть</p></title>"
      "<section id=\"book\"><title><p>Книга</p></title><section id=\"ch1\"><title><p>Глава 1</p></title>"
      "<p>текст</p></section><section id=\"ch2\"><title><p>Глава 2</p></title><p>текст</p></section>"
      "</section></section><section id=\"next\"><epigraph><p>эпиграф</p></epigraph>"
      "<section id=\"ch3\"><title><p>Глава 3</p></title><p>текст</p></section></section></body></FictionBook>");
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  const std::string& ncx = out.files["OEBPS/toc.ncx"];
  EXPECT_TRUE(contains(ncx, "<text>Часть</text></navLabel><content src=\"s2.xhtml\"/>")) << ncx;
  EXPECT_TRUE(contains(ncx, "<text>Книга</text></navLabel><content src=\"s2.xhtml\"/>"));
  EXPECT_TRUE(contains(ncx, "<text>Глава 1</text></navLabel><content src=\"s2.xhtml\"/>"));
  EXPECT_TRUE(contains(ncx, "<text>Глава 2</text></navLabel><content src=\"s2.xhtml#ch2\"/>"));
  EXPECT_TRUE(contains(ncx, "<text>Глава 3</text></navLabel><content src=\"s3.xhtml#ch3\"/>"));
  // Ids stay in place for links.
  EXPECT_TRUE(contains(out.files["OEBPS/s2.xhtml"], "<div class=\"section\" id=\"ch1\">"));
}

TEST(Fb2ToEpub, KeepsDeepTitlesAtTextSize) {
  StringInput input(
      "<FictionBook><body><section><section><section><section><title><p>4</p></title><poem><title><p>П</p>"
      "</title><stanza><v>в</v></stanza></poem></section></section></section></section></body></FictionBook>");
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  const std::string& s1 = out.files["OEBPS/s1.xhtml"];
  EXPECT_TRUE(contains(s1, "<h4 class=\"title\">4</h4><div class=\"poem\"><h4 class=\"title\">П</h4>"));
  EXPECT_TRUE(wellFormed(s1));
}

TEST(Fb2ToEpub, LeavesNoteMarkersOutOfTocLabels) {
  StringInput input(
      "<FictionBook xmlns:l=\"x\"><body><section><title><p>5. Против академиков<a l:href=\"#n1\" "
      "type=\"note\">[31]</a></p></title><p>текст</p></section></body><body name=\"notes\">"
      "<section id=\"n1\"><p>сноска</p></section></body></FictionBook>");
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  EXPECT_TRUE(contains(out.files["OEBPS/toc.ncx"], "<text>5. Против академиков</text>"));
  // The heading keeps its marker and link.
  EXPECT_TRUE(contains(out.files["OEBPS/s1.xhtml"], "академиков<a href=\"s2.xhtml#n1\"><sup>[31]</sup></a></h2>"));
}

TEST(Fb2ToEpub, DecodesCoverAndDeclaresIt) {
  StringInput input(sampleBook("x"));
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  EXPECT_EQ(out.files["OEBPS/img0.jpg"], COVER_BYTES);
  EXPECT_EQ(out.files.count("OEBPS/img1.bmp"), 0u);

  const std::string& opf = out.files["OEBPS/content.opf"];
  EXPECT_TRUE(contains(opf, "<dc:title>Тестовая книга</dc:title>"));
  EXPECT_TRUE(contains(opf, "<dc:creator opf:role=\"aut\">Лев Николаевич Толстой</dc:creator>"));
  EXPECT_TRUE(contains(opf, "<dc:language>ru</dc:language>"));
  EXPECT_TRUE(contains(opf, "<meta name=\"cover\" content=\"img0\"/>"));
  EXPECT_TRUE(contains(opf, "<item id=\"img0\" href=\"img0.jpg\" media-type=\"image/jpeg\"/>"));
  EXPECT_TRUE(contains(opf, "<spine toc=\"ncx\"><itemref idref=\"cover\"/><itemref idref=\"s1\"/>"));
  EXPECT_TRUE(contains(out.files["OEBPS/cover.xhtml"], "<img src=\"img0.jpg\" alt=\"\"/>"));
}

TEST(Fb2ToEpub, SkipsTheCoverPageWhenTheBodyOpensWithTheCover) {
  // calibre's output: the <coverpage> image again as the body's first content.
  StringInput input(
      "<FictionBook xmlns:l=\"x\"><description><title-info><book-title>К</book-title>"
      "<coverpage><image l:href=\"#c.jpg\"/></coverpage></title-info></description>"
      "<body><section><p>\n    <image l:href=\"#c.jpg\"/></p></section><section><p>текст</p></section></body>"
      "<binary id=\"c.jpg\" content-type=\"image/jpeg\">" +
      COVER_BASE64 + "</binary></FictionBook>");
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  EXPECT_EQ(out.files.count("OEBPS/cover.xhtml"), 0u);
  EXPECT_TRUE(contains(out.files["OEBPS/s1.xhtml"], "<img src=\"img0.jpg\" alt=\"\"/>"));
  const std::string& opf = out.files["OEBPS/content.opf"];
  EXPECT_TRUE(contains(opf, "<meta name=\"cover\" content=\"img0\"/>"));
  EXPECT_FALSE(contains(opf, "cover.xhtml"));
  EXPECT_TRUE(contains(opf, "<spine toc=\"ncx\"><itemref idref=\"s1\"/>"));
  EXPECT_FALSE(contains(out.files["OEBPS/toc.ncx"], "cover.xhtml"));
  EXPECT_TRUE(wellFormed(opf));
}

TEST(Fb2ToEpub, InlineAndBlockImages) {
  StringInput input(
      "<FictionBook xmlns:l=\"x\"><body><section><image l:href=\"#a\"/><p>до <image l:href=\"#a\"/> после</p>"
      "<image l:href=\"#missing\"/></section></body><binary id=\"a\" content-type=\"image/png\">iVBO</binary>"
      "</FictionBook>");
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  const std::string& s1 = out.files["OEBPS/s1.xhtml"];
  EXPECT_TRUE(contains(s1, "<div class=\"image\"><img src=\"img0.png\" alt=\"\"/></div><p>до <img src=\"img0.png\""));
  EXPECT_FALSE(contains(s1, "missing"));
  EXPECT_TRUE(wellFormed(s1));
  // No <coverpage>: no cover page, and the TOC falls back to one entry.
  EXPECT_EQ(out.files.count("OEBPS/cover.xhtml"), 0u);
  EXPECT_TRUE(contains(out.files["OEBPS/toc.ncx"], "<content src=\"s1.xhtml\"/>"));
}

TEST(Fb2ToEpub, DecodesWindows1251) {
  // "Привет" and "Глава" in windows-1251.
  StringInput input(
      "<?xml version=\"1.0\" encoding=\"windows-1251\"?><FictionBook><description><title-info>"
      "<book-title>\xcf\xf0\xe8\xe2\xe5\xf2</book-title></title-info></description>"
      "<body><section><title><p>\xc3\xeb\xe0\xe2\xe0</p></title><p>\xcf\xf0\xe8\xe2\xe5\xf2</p></section></body>"
      "</FictionBook>");
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  EXPECT_EQ(converter.metadata().title, "Привет");
  EXPECT_TRUE(contains(out.files["OEBPS/s1.xhtml"], "<p>Привет</p>"));
  EXPECT_TRUE(contains(out.files["OEBPS/toc.ncx"], "<text>Глава</text>"));
}

TEST(Fb2ToEpub, StartsEveryTopLevelSectionOnANewPage) {
  // Short untitled front matter, as calibre writes it: each top-level section is a page of its own,
  // while one holding only whitespace is no page.
  StringInput input(
      "<FictionBook><body><section><p> "
      "</p></section><section><p>Титул</p></section><section><p>Посвящение</p></section>"
      "<section><p>Введение</p><section><p>Сцена</p></section></section></body>"
      "<body name=\"notes\"><section id=\"n1\"><p>1</p></section><section id=\"n2\"><p>2</p></section></body>"
      "</FictionBook>");
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  EXPECT_TRUE(contains(out.files["OEBPS/s1.xhtml"], "Титул"));
  EXPECT_TRUE(contains(out.files["OEBPS/s2.xhtml"], "Посвящение"));
  // A short nested section and short notes still share a file.
  EXPECT_TRUE(contains(out.files["OEBPS/s3.xhtml"], "<p>Введение</p><div class=\"section\" id=\"fb2s5\"><p>Сцена"));
  EXPECT_TRUE(contains(out.files["OEBPS/s4.xhtml"], "<p>1</p></div><div class=\"section\" id=\"n2\">"));
  EXPECT_EQ(out.files.count("OEBPS/s5.xhtml"), 0u);
}

TEST(Fb2ToEpub, SetsAStanzaAsOneParagraph) {
  StringInput input(
      "<FictionBook><body><section><poem><stanza><title><p>I</p></title><v>Мороз и <emphasis>солнце</emphasis>;"
      "</v><v id=\"v2\">день чудесный!</v><subtitle>*</subtitle><v>Ещё ты дремлешь</v></stanza>"
      "<stanza><v>Пора</v></stanza><v>вне строфы</v></poem></section></body></FictionBook>");
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  const std::string& s1 = out.files["OEBPS/s1.xhtml"];
  EXPECT_TRUE(contains(s1,
                       "<div class=\"stanza\"><h3 class=\"title\">I</h3><p class=\"stanza\">Мороз и <em>солнце</em>;"
                       "<br/><a id=\"v2\"></a>день чудесный!</p><h4 class=\"subtitle\">*</h4><p class=\"stanza\">Ещё "
                       "ты дремлешь</p></div>"
                       "<div class=\"stanza\"><p class=\"stanza\">Пора</p></div><p class=\"v\">вне строфы</p>"))
      << s1;
  EXPECT_TRUE(wellFormed(s1));
}

TEST(Fb2ToEpub, KeepsOddStanzasWellFormed) {
  const char* bodies[] = {
      "<poem><stanza><v>a<stanza><v>b</v></stanza></v><v>c</v></stanza></poem>",  // in a verse
      "<poem><stanza><v>a</v><stanza><v>b</v></stanza><v>c</v></stanza></poem>",  // in a stanza
      "<p>a<stanza><v>b</v></stanza>c</p><stanza><v>d</v><empty-line/><v>e</v></stanza>",
      "<title><p>a<stanza><v>b</v></stanza></p></title><stanza><title><p>c</p></title></stanza>",
  };
  for (const char* body : bodies) {
    StringInput input(std::string("<FictionBook><body><section>") + body + "</section></body></FictionBook>");
    MapOutput out;
    Fb2ToEpub converter;
    ASSERT_TRUE(converter.convert(input, out)) << body;
    const std::string& s1 = out.files["OEBPS/s1.xhtml"];
    EXPECT_TRUE(wellFormed(s1)) << body << "\n" << s1;
    EXPECT_TRUE(contains(s1, "b")) << s1;
  }
}

TEST(Fb2ToEpub, SetsSubtitlesAsHeadings) {
  StringInput input(
      "<FictionBook><body><section><p>до</p><subtitle>* * *</subtitle><p>после</p></section></body>"
      "</FictionBook>");
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  EXPECT_TRUE(contains(out.files["OEBPS/s1.xhtml"], "<p>до</p><h4 class=\"subtitle\">* * *</h4><p>после</p>"));
}

TEST(Fb2ToEpub, CapsRunsOfEmptyLines) {
  StringInput input("<FictionBook><body><section><p>Титул</p>" + repeat("<empty-line/>", 21) +
                    "<p>Москва</p><empty-line/><p>2020</p><empty-line/><empty-line/></section><section>" +
                    repeat("<empty-line/>", 3) + "<p>Посвящение</p></section></body></FictionBook>");
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  const std::string blank = "<p class=\"empty-line\">&#160;</p>";
  EXPECT_TRUE(
      contains(out.files["OEBPS/s1.xhtml"], "<p>Титул</p>" + blank + blank + "<p>Москва</p>" + blank + "<p>2020</p>"));
  // A new page keeps its own top spacing after a run that ended the page before.
  EXPECT_TRUE(contains(out.files["OEBPS/s2.xhtml"],
                       "<body><div class=\"section\" id=\"fb2s2\">" + blank + blank + "<p>Посвящение</p>"));
}

TEST(Fb2ToEpub, BoundsPagesForShortSections) {
  StringInput input("<FictionBook><body>" + repeat("<section><p>x</p></section>", 600) + "</body></FictionBook>",
                    65536);
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  size_t files = 0;
  for (const auto& [name, data] : out.files) {
    if (name.ends_with(".xhtml")) files++;
  }
  EXPECT_EQ(files, Fb2ToEpub::MAX_OWN_PAGE_FILES);
  EXPECT_TRUE(wellFormed(out.files["OEBPS/s512.xhtml"]));
}

TEST(Fb2ToEpub, SyntheticSectionIdsLeaveTheLinkTableAlone) {
  // Untitled, id-less sections used to fill the id table and drop real targets after them.
  StringInput input("<FictionBook xmlns:l=\"x\"><body><section><p><a l:href=\"#late\">ссылка</a></p>" +
                        repeat("<section><p>.</p></section>", Fb2ToEpub::MAX_IDS) +
                        "<p id=\"late\">цель</p></section></body></FictionBook>",
                    65536);
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  EXPECT_TRUE(contains(out.files["OEBPS/s1.xhtml"], "#late\">ссылка</a>"));
}

TEST(Fb2ToEpub, SplitsAnOversizedSectionBetweenParagraphs) {
  const std::string para = "<p>" + repeat("слово ", 200) + "</p>";  // ~2.4 KB of text
  StringInput input("<FictionBook><body><section><section><title><p>Одна</p></title>" + repeat(para, 100) +
                        "</section></section></body></FictionBook>",
                    4096);
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  int files = 0;
  for (const auto& [name, data] : out.files) {
    if (!name.ends_with(".xhtml")) continue;
    files++;
    EXPECT_TRUE(wellFormed(data)) << name;
  }
  EXPECT_GE(files, 3);
  EXPECT_TRUE(contains(out.files["OEBPS/s2.xhtml"], "<body><div class=\"section\"><div class=\"section\"><p>"));
}

TEST(Fb2ToEpub, KeepsWhatPrecedesAnXmlError) {
  std::string doc = sampleBook(LONG_TEXT);
  doc.resize(doc.find("Строка два"));
  doc += "<broken attr=>";
  StringInput input(doc);
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  ASSERT_EQ(out.files.count("OEBPS/s3.xhtml"), 1u);
  EXPECT_TRUE(contains(out.files["OEBPS/s3.xhtml"], "Строка один"));
  for (const auto& [name, data] : out.files) {
    if (name.ends_with(".xhtml") || name.ends_with(".opf") || name.ends_with(".ncx")) {
      EXPECT_TRUE(wellFormed(data)) << name << "\n" << data;
    }
  }
}

TEST(Fb2ToEpub, MetadataRejectsMalformedOrTruncatedDescription) {
  Fb2ToEpub::Metadata meta;
  StringInput malformed(
      "<FictionBook><description><title-info><book-title>Т</book-title><annotation><p>x</q></annotation>"
      "</title-info></description><body><p>текст</p></body></FictionBook>");
  EXPECT_FALSE(Fb2ToEpub::readMetadata(malformed, meta));
  StringInput truncated("<FictionBook><description><title-info><book-title>Т</book-title>");
  EXPECT_FALSE(Fb2ToEpub::readMetadata(truncated, meta));
}

TEST(Fb2ToEpub, BoundsMetadataOnCharacterBoundaries) {
  std::string authors;
  for (size_t i = 0; i < Fb2ToEpub::MAX_AUTHORS + 4; i++) authors += "<author><nickname>А</nickname></author>";
  StringInput input("<FictionBook><description><title-info>" + authors + "<book-title>1" + repeat("Ж", 1000) +
                        "</book-title></title-info></description></FictionBook>",
                    4096);
  Fb2ToEpub::Metadata meta;
  ASSERT_TRUE(Fb2ToEpub::readMetadata(input, meta));
  EXPECT_EQ(meta.title, "1" + repeat("Ж", 255));
  EXPECT_EQ(meta.authors.size(), Fb2ToEpub::MAX_AUTHORS);
}

TEST(Fb2ToEpub, TruncatesLongTocLabelsOnCharacterBoundaries) {
  StringInput input("<FictionBook><body><section><title><p>1" + repeat("Ж", 150) +
                    "</p></title><p>текст</p></section></body></FictionBook>");
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  const std::string& ncx = out.files["OEBPS/toc.ncx"];
  EXPECT_TRUE(wellFormed(ncx)) << ncx;
  EXPECT_TRUE(contains(ncx, "<text>1" + repeat("Ж", 99) + "</text>"));
}

TEST(Fb2ToEpub, EndsTheBookAtTheNestingLimit) {
  StringInput input("<FictionBook><body><section><p>до</p>" + repeat("<cite>", Fb2ToEpub::MAX_DEPTH) +
                        "<p>глубоко</p>" + repeat("</cite>", Fb2ToEpub::MAX_DEPTH) + "</section></body></FictionBook>",
                    4096);
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  const std::string& s1 = out.files["OEBPS/s1.xhtml"];
  EXPECT_TRUE(contains(s1, "<p>до</p>"));
  EXPECT_FALSE(contains(s1, "глубоко"));
  EXPECT_TRUE(wellFormed(s1));
}

TEST(Fb2ToEpub, EndsTheBookAtTheFileLimit) {
  StringInput input("<FictionBook>" + repeat("<body><p>x</p></body>", Fb2ToEpub::MAX_FILES + 10) + "</FictionBook>",
                    65536);
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  size_t files = 0;
  for (const auto& [name, data] : out.files) {
    if (name.ends_with(".xhtml")) files++;
  }
  EXPECT_EQ(files, Fb2ToEpub::MAX_FILES);
  EXPECT_TRUE(wellFormed(out.files["OEBPS/content.opf"]));
}

TEST(Fb2ToEpub, LinksToIdsPastTheLimitStayText) {
  std::string paragraphs;
  for (size_t i = 0; i < Fb2ToEpub::MAX_IDS; i++) paragraphs += "<p id=\"p" + std::to_string(i) + "\">.</p>";
  StringInput input(
      "<FictionBook xmlns:l=\"x\"><body><section><p><a l:href=\"#p1\">ранняя</a> <a l:href=\"#late\">"
      "поздняя</a></p>" +
          paragraphs +
          "</section></body><body name=\"notes\"><section id=\"late\"><p>сноска</p>"
          "</section></body></FictionBook>",
      65536);
  MapOutput out;
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, out));
  const std::string& s1 = out.files["OEBPS/s1.xhtml"];
  EXPECT_TRUE(contains(s1, "<a href=\"s1.xhtml#p1\">ранняя</a>"));
  EXPECT_TRUE(contains(s1, "</a> поздняя</p>"));
}

class MemoryFile : public StoredZipWriter::File {
 public:
  bool write(const uint8_t* p, size_t len) override {
    if (data.size() < pos + len) data.resize(pos + len);
    memcpy(&data[pos], p, len);
    pos += len;
    return true;
  }
  bool seek(uint32_t to) override {
    pos = to;
    return to <= data.size();
  }
  std::string data;
  size_t pos = 0;
};

uint32_t rd32(const std::string& s, size_t at) {
  return static_cast<uint8_t>(s[at]) | static_cast<uint8_t>(s[at + 1]) << 8 | static_cast<uint8_t>(s[at + 2]) << 16 |
         static_cast<uint32_t>(static_cast<uint8_t>(s[at + 3])) << 24;
}
uint16_t rd16(const std::string& s, size_t at) {
  return static_cast<uint16_t>(static_cast<uint8_t>(s[at]) | static_cast<uint8_t>(s[at + 1]) << 8);
}

struct ZipEntry {
  std::string name;
  std::string data;
};

// Reads entries through the central directory, checking each local header and CRC.
std::vector<ZipEntry> readZip(const std::string& zip) {
  std::vector<ZipEntry> entries;
  const size_t eocd = zip.size() - 22;
  EXPECT_EQ(rd32(zip, eocd), 0x06054b50u);
  const uint16_t count = rd16(zip, eocd + 10);
  size_t at = rd32(zip, eocd + 16);
  for (int i = 0; i < count; i++) {
    EXPECT_EQ(rd32(zip, at), 0x02014b50u);
    EXPECT_EQ(rd16(zip, at + 10), 0);  // stored
    const uint32_t crc = rd32(zip, at + 16);
    const uint32_t size = rd32(zip, at + 24);
    const uint16_t nameLen = rd16(zip, at + 28);
    const uint32_t local = rd32(zip, at + 42);
    ZipEntry e{zip.substr(at + 46, nameLen), ""};
    EXPECT_EQ(rd32(zip, local), 0x04034b50u);
    EXPECT_EQ(rd32(zip, local + 14), crc);
    EXPECT_EQ(rd32(zip, local + 22), size);
    e.data = zip.substr(local + 30 + rd16(zip, local + 26) + rd16(zip, local + 28), size);
    EXPECT_EQ(mz_crc32(0, reinterpret_cast<const uint8_t*>(e.data.data()), e.data.size()), crc) << e.name;
    entries.push_back(e);
    at += 46 + nameLen;
  }
  return entries;
}

TEST(StoredZipWriter, WritesReadableStoredEntries) {
  MemoryFile file;
  StoredZipWriter zip(file);
  const std::string big(10000, 'x');
  ASSERT_TRUE(zip.beginFile("mimetype"));
  ASSERT_TRUE(zip.write("application/epub+zip", 20));
  ASSERT_TRUE(zip.endFile());
  ASSERT_TRUE(zip.beginFile("OEBPS/big.txt"));
  ASSERT_TRUE(zip.write(big.data(), 5000));
  ASSERT_TRUE(zip.write(big.data(), 5000));
  ASSERT_TRUE(zip.endFile());
  ASSERT_TRUE(zip.finish());

  // EPUB requires the mimetype first and uncompressed, readable at a fixed offset.
  EXPECT_EQ(file.data.substr(30, 28), "mimetypeapplication/epub+zip");
  const auto entries = readZip(file.data);
  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries[1].name, "OEBPS/big.txt");
  EXPECT_EQ(entries[1].data, big);
}

TEST(StoredZipWriter, HoldsAConvertedBook) {
  StringInput input(sampleBook(LONG_TEXT));
  MemoryFile file;
  StoredZipWriter zip(file);
  Fb2ToEpub converter;
  ASSERT_TRUE(converter.convert(input, zip));
  ASSERT_TRUE(zip.finish());
  const auto entries = readZip(file.data);
  ASSERT_EQ(entries.size(), 11u);
  EXPECT_EQ(entries[0].name, "mimetype");
  EXPECT_EQ(entries[8].data, COVER_BYTES);
}

}  // namespace
