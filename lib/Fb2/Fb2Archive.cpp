// Adapted from guloff/folio-firmware (MIT)

#include "Fb2Archive.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <ZipFile.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "Fb2ToEpub.h"
#include "StoredZipWriter.h"

namespace {

constexpr size_t READ_CHUNK = 4096;

// A pass over a large book runs for seconds: let the idle task feed the watchdog.
void yieldEvery(uint32_t& chunks) {
  if ((++chunks & 0x0F) == 0) vTaskDelay(1);
}

class FileInput : public Fb2ToEpub::Input {
 public:
  explicit FileInput(const std::string& path) : path(path) {}

  bool stream(Fb2ToEpub::Sink sink, void* ctx) override {
    HalFile file;
    if (!Storage.openFileForRead("FB2", path, file)) return false;
    auto chunk = makeUniqueNoThrow<char[]>(READ_CHUNK);
    if (!chunk) {
      LOG_ERR("FB2", "OOM: read buffer");
      return false;
    }
    uint32_t chunks = 0;
    while (true) {
      const int n = file.read(chunk.get(), READ_CHUNK);
      if (n < 0) return false;
      if (n == 0) return true;
      if (!sink(ctx, chunk.get(), static_cast<size_t>(n))) return true;
      yieldEvery(chunks);
    }
  }

 private:
  const std::string& path;
};

// The first .fb2 entry of a .fb2.zip, inflated on the fly.
class ZipInput : public Fb2ToEpub::Input {
 public:
  explicit ZipInput(const std::string& path) : path(path) {}

  bool stream(Fb2ToEpub::Sink sink, void* ctx) override {
    ZipFile zip(path);
    if (entry.empty()) {
      zip.enumerateFilePaths([this](std::string_view name) {
        if (entry.empty() && FsHelpers::checkFileExtension(name, ".fb2")) entry = std::string(name);
      });
      if (entry.empty()) {
        LOG_ERR("FB2", "No .fb2 inside %s", path.c_str());
        return false;
      }
    }
    SinkPrint out(sink, ctx);
    return zip.readFileToStream(entry.c_str(), out, READ_CHUNK, /*allowEarlyStop=*/true);
  }

 private:
  class SinkPrint : public Print {
   public:
    SinkPrint(Fb2ToEpub::Sink sink, void* ctx) : sink(sink), ctx(ctx) {}
    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t* data, size_t len) override {
      yieldEvery(chunks);
      return sink(ctx, reinterpret_cast<const char*>(data), len) ? len : 0;
    }

   private:
    Fb2ToEpub::Sink sink;
    void* ctx;
    uint32_t chunks = 0;
  };

  const std::string& path;
  std::string entry;
};

class SdZipFile : public StoredZipWriter::File {
 public:
  explicit SdZipFile(HalFile& file) : file(file) {}
  bool write(const uint8_t* data, size_t len) override { return file.write(data, len) == len; }
  bool seek(uint32_t pos) override { return file.seek(pos); }

 private:
  HalFile& file;
};

bool isZipped(const std::string& path) { return FsHelpers::checkFileExtension(path, ".zip"); }

// The cover image, written where the EPUB path extracts its own: "<dir>/.cover.jpg" or ".png".
class CoverFile : public Fb2ToEpub::Output {
 public:
  explicit CoverFile(const std::string& dir) : dir(dir) {}
  bool beginFile(const char* name) override {
    path = dir + "/." + name;
    return Storage.openFileForWrite("FB2", path, file);
  }
  bool write(const char* data, size_t len) override { return file.write(data, len) == len; }
  bool endFile() override { return file.close(); }
  std::string path;

 private:
  const std::string& dir;
  HalFile file;
};

bool convertFrom(Fb2ToEpub::Input& input, const std::string& tmpPath) {
  HalFile file;
  if (!Storage.openFileForWrite("FB2", tmpPath, file)) return false;
  SdZipFile sdFile(file);
  StoredZipWriter zip(sdFile);
  auto converter = makeUniqueNoThrow<Fb2ToEpub>();
  if (!converter) {
    LOG_ERR("FB2", "OOM: converter");
    return false;
  }
  const bool written = converter->convert(input, zip) && zip.finish();
  // close() flushes the last sectors; an archive that failed to flush is not moved into place.
  return file.close() && written;
}

}  // namespace

namespace Fb2Archive {

bool convert(const std::string& fb2Path, const std::string& epubPath) {
  const uint32_t start = millis();
  const std::string tmpPath = epubPath + ".tmp";
  bool ok;
  if (isZipped(fb2Path)) {
    ZipInput input(fb2Path);
    ok = convertFrom(input, tmpPath);
  } else {
    FileInput input(fb2Path);
    ok = convertFrom(input, tmpPath);
  }
  if (!ok) {
    LOG_ERR("FB2", "Could not convert %s", fb2Path.c_str());
    Storage.remove(tmpPath.c_str());
    return false;
  }
  Storage.remove(epubPath.c_str());
  if (!Storage.rename(tmpPath.c_str(), epubPath.c_str())) {
    LOG_ERR("FB2", "Could not move %s into place", epubPath.c_str());
    Storage.remove(tmpPath.c_str());
    return false;
  }
  LOG_DBG("FB2", "Converted %s in %lu ms", fb2Path.c_str(), static_cast<unsigned long>(millis() - start));
  return true;
}

std::string extractCover(const std::string& fb2Path, const std::string& dir) {
  auto out = makeUniqueNoThrow<CoverFile>(dir);
  if (!out) return {};
  const char* extension;
  if (isZipped(fb2Path)) {
    ZipInput input(fb2Path);
    extension = Fb2ToEpub::readCover(input, *out);
  } else {
    FileInput input(fb2Path);
    extension = Fb2ToEpub::readCover(input, *out);
  }
  if (!extension) {
    if (!out->path.empty()) {
      out->endFile();  // a truncated binary leaves the file open
      Storage.remove(out->path.c_str());
    }
    return {};
  }
  return out->path;
}

bool readMetadata(const std::string& fb2Path, std::string& title, std::string& author) {
  Fb2ToEpub::Metadata meta;
  bool ok;
  if (isZipped(fb2Path)) {
    ZipInput input(fb2Path);
    ok = Fb2ToEpub::readMetadata(input, meta);
  } else {
    FileInput input(fb2Path);
    ok = Fb2ToEpub::readMetadata(input, meta);
  }
  if (!ok) return false;
  title = std::move(meta.title);
  author.clear();
  for (const auto& name : meta.authors) {
    if (!author.empty()) author += ", ";
    author += name;
  }
  return true;
}

}  // namespace Fb2Archive
