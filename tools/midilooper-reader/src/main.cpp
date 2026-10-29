#include <Arduino.h>
#include <SD.h>

// No looper startup, recovery, persistence, MIDI, or SD mutation commands are linked.
namespace {
constexpr char kRoot[] = "/MidiLooper";
constexpr unsigned kMaxDepth = 16;
constexpr size_t kMaxPath = 512;
char command[96];  // DEC-190 C: `MLREADER_FILE <name>` needs room for a 48-char .txt name
size_t used = 0;
bool overflow = false;
uint32_t files = 0;
uint64_t bytes = 0;

bool acknowledge() {
  uint32_t start = millis();
  unsigned count = 0;
  const char expected[] = "OK\n";
  while (Serial && millis() - start < 30000) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c != expected[count++]) return false;
      if (count == 3) return true;
    }
    yield();
  }
  return false;
}

void hex(const uint8_t* data, size_t size) {
  static const char digits[] = "0123456789abcdef";
  char encoded[128];
  while (size) {
    size_t n = min(size, sizeof(encoded) / 2);
    for (size_t i = 0; i < n; ++i) {
      encoded[2*i] = digits[data[i] >> 4];
      encoded[2*i+1] = digits[data[i] & 15];
    }
    Serial.write(reinterpret_cast<uint8_t*>(encoded), 2*n);
    data += n;
    size -= n;
  }
}

uint32_t crcUpdate(uint32_t crc, const uint8_t* data, size_t n) {
  while (n--) {
    crc ^= *data++;
    for (unsigned b = 0; b < 8; ++b)
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return crc;
}

bool fail(const char* reason) {
  Serial.print("ERROR "); Serial.println(reason);
  return false;
}

bool walk(File& dir, const char* relative, unsigned depth) {
  if (depth > kMaxDepth) return fail("directory_depth");
  while (Serial) {
    File entry = dir.openNextFile(FILE_READ);
    if (!entry) return SD.sdfs.sdErrorCode() ? fail("directory_read") : true;
    const char* name = entry.name();
    if (!name || !*name || strchr(name, '/') || strchr(name, '\\') ||
        !strcmp(name, ".") || !strcmp(name, "..")) return fail("invalid_name");
    char path[kMaxPath];
    int n = snprintf(path, sizeof(path), "%s%s%s", relative, *relative ? "/" : "", name);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(path)) return fail("path_too_long");
    if (entry.isDirectory()) {
      Serial.print("DIR "); hex(reinterpret_cast<uint8_t*>(path), n); Serial.println();
      if (!acknowledge()) return false;
      if (!walk(entry, path, depth + 1)) return false;
    } else {
      const uint64_t length = entry.size();
      Serial.print("FILE "); hex(reinterpret_cast<uint8_t*>(path), n);
      Serial.printf(" %llu\n", static_cast<unsigned long long>(length));
      if (!acknowledge()) return false;
      uint64_t remaining = length;
      uint32_t crc = 0xffffffffu;
      uint8_t buffer[256];
      while (remaining) {
        if (!Serial) return false;
        size_t want = remaining < sizeof(buffer) ? static_cast<size_t>(remaining) : sizeof(buffer);
        int got = entry.read(buffer, want);
        if (got <= 0) return fail("file_read");
        crc = crcUpdate(crc, buffer, got);
        Serial.print("DATA "); hex(buffer, got); Serial.println();
        if (!acknowledge()) return false;
        remaining -= got;
      }
      Serial.printf("END %08lx\n", static_cast<unsigned long>(crc ^ 0xffffffffu));
      if (!acknowledge()) return false;
      ++files; bytes += length;
    }
    entry.close();
  }
  return false;
}

// DEC-190 option C: read-only export of a small **root-level** diagnostic file (`crashlog.txt`), which the
// folder walk cannot reach because its root is `/MidiLooper`. Whitelisted on purpose: this image must never be
// able to enumerate or read arbitrary card content. Same FILE/DATA/END framing as a folder entry, so the host
// reuses one decoder and one CRC check.
// Root reads are restricted to `*.txt` on purpose: the card carries many unrelated files and this image must
// never enumerate or read arbitrary card content. Names are plain (no separators), short and bounded.
constexpr unsigned kMaxRootTxtName = 48;

bool isAllowedRootFile(const char* name) {
  if (name == nullptr || !*name || strchr(name, '/') || strchr(name, '\\')) return false;
  const size_t length = strlen(name);
  return length > 4 && length <= kMaxRootTxtName && !strcmp(name + length - 4, ".txt");
}

// Bounded listing of root `*.txt` entries only. One line per name with its size, so the host can fetch each
// through `MLREADER_FILE`. Nothing else at the root is ever named.
void listRootTxtFiles() {
  if (!SD.begin(BUILTIN_SDCARD)) { fail("sd_unavailable"); return; }
  File root = SD.open("/", FILE_READ);
  if (!root || !root.isDirectory()) { fail("root_unavailable"); return; }
  Serial.println("TXTLIST");
  if (!acknowledge()) { root.close(); return; }
  unsigned count = 0;
  while (count < 16) {
    File entry = root.openNextFile(FILE_READ);
    if (!entry) break;
    const char* name = entry.name();
    if (name != nullptr && !entry.isDirectory() && isAllowedRootFile(name)) {
      Serial.print("TXT "); hex(reinterpret_cast<const uint8_t*>(name), strlen(name));
      Serial.printf(" %llu\n", static_cast<unsigned long long>(entry.size()));
      if (!acknowledge()) { entry.close(); root.close(); return; }
      ++count;
    }
    entry.close();
  }
  root.close();
  Serial.println("ENDTXTLIST");
  acknowledge();
}

void exportRootFile(const char* name) {
  if (!isAllowedRootFile(name)) { fail("root_file_not_allowed"); return; }
  if (!SD.begin(BUILTIN_SDCARD)) { fail("sd_unavailable"); return; }
  File file = SD.open(name, FILE_READ);
  if (!file || file.isDirectory()) { fail("root_file_missing"); return; }
  const uint64_t length = file.size();
  const size_t nameLength = strlen(name);
  Serial.print("FILE "); hex(reinterpret_cast<const uint8_t*>(name), nameLength);
  Serial.printf(" %llu\n", static_cast<unsigned long long>(length));
  if (!acknowledge()) { file.close(); return; }
  uint64_t remaining = length;
  uint32_t crc = 0xffffffffu;
  uint8_t buffer[256];
  while (remaining) {
    size_t want = remaining < sizeof(buffer) ? static_cast<size_t>(remaining) : sizeof(buffer);
    int got = file.read(buffer, want);
    if (got <= 0) { file.close(); fail("file_read"); return; }
    crc = crcUpdate(crc, buffer, got);
    Serial.print("DATA "); hex(buffer, got); Serial.println();
    if (!acknowledge()) { file.close(); return; }
    remaining -= got;
  }
  file.close();
  Serial.printf("END %08lx\n", static_cast<unsigned long>(crc ^ 0xffffffffu));
  acknowledge();
}

void exportFolder() {
  files = 0; bytes = 0;
  // Mount only on explicit export. All opens are read-only; never enumerate card root.
  if (!SD.begin(BUILTIN_SDCARD)) { fail("sd_unavailable"); return; }
  File root = SD.open(kRoot, FILE_READ);
  if (!root || !root.isDirectory()) { fail("MidiLooper_missing"); return; }
  Serial.println("BEGIN MLREADER2");
  if (!acknowledge()) return;
  if (walk(root, "", 0)) {
    Serial.printf("DONE %lu %llu\n", static_cast<unsigned long>(files),
                  static_cast<unsigned long long>(bytes));
    acknowledge();
  }
  root.close();
}
}  // namespace

void setup() { Serial.begin(115200); }
void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      command[used] = 0;
      if (!overflow && !strcmp(command, "MLREADER_HELLO")) Serial.println("MLREADER2 READ_ONLY /MidiLooper");
      else if (!overflow && !strcmp(command, "MLREADER_EXPORT")) exportFolder();
      else if (!overflow && !strncmp(command, "MLREADER_FILE ", 14)) exportRootFile(command + 14);
      else if (!overflow && !strcmp(command, "MLREADER_LS_TXT")) listRootTxtFiles();
      else fail("unknown_command");
      used = 0; overflow = false;
    } else if (used < sizeof(command)-1) command[used++] = c;
    else overflow = true;
  }
}
