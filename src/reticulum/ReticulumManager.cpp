// Direct port from Ratputer — microReticulum integration
#include "ReticulumManager.h"
#include "config/Config.h"
#include "util/PerfTrace.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <unordered_map>
#include <string>
#include <cstring>
#include <utility>

uint32_t ReticulumManager::_announceFilterCount = 0;

bool LittleFSFileSystem::init() { return true; }
bool LittleFSFileSystem::file_exists(const char* p) { return LittleFS.exists(p); }

size_t LittleFSFileSystem::read_file(const char* p, RNS::Bytes& data) {
    File f = LittleFS.open(p, "r");
    if (!f) return 0;
    size_t s = f.size();
    data = RNS::Bytes(s);
    f.readBytes((char*)data.writable(s), s);
    f.close();
    return s;
}

size_t LittleFSFileSystem::write_file(const char* p, const RNS::Bytes& data) {
    unsigned long traceStart = PerfTrace::nowMs();
    String path = String(p);
    int lastSlash = path.lastIndexOf('/');
    if (lastSlash > 0) {
        String dir = path.substring(0, lastSlash);
        if (!LittleFS.exists(dir.c_str())) { LittleFS.mkdir(dir.c_str()); }
    }
    File f = LittleFS.open(p, "w");
    if (!f) {
        PerfTrace::logWrite("flash", "rns-write-file", p, data.size(), false,
                            PerfTrace::elapsedMs(traceStart));
        return 0;
    }
    size_t w = f.write(data.data(), data.size());
    f.close();
    PerfTrace::logWrite("flash", "rns-write-file", p, data.size(), w == data.size(),
                        PerfTrace::elapsedMs(traceStart));
    return w;
}

// Arduino-LittleFS wrapper for RNS::FileStreamImpl. Needed by microReticulum's
// Persistence::serialize<std::map<...>> streaming path in Transport::write_path_table.
// Without this, write_path_table always logs "serialize failed" because the
// default stub returns NONE, and the path table never persists across reboots.
namespace {
constexpr size_t RNS_COPY_CHUNK = 512;

bool copySDToFlash(const char* sdPath, const char* flashPath) {
    unsigned long traceStart = PerfTrace::nowMs();
    size_t copied = 0;
    auto finish = [&](bool ok) -> bool {
        PerfTrace::logWrite("flash", "copy-sd-to-flash", flashPath, copied, ok,
                            PerfTrace::elapsedMs(traceStart));
        return ok;
    };

    File in = SD.open(sdPath, FILE_READ);
    if (!in) return finish(false);

    String path = String(flashPath);
    int lastSlash = path.lastIndexOf('/');
    if (lastSlash > 0) {
        String dir = path.substring(0, lastSlash);
        if (!LittleFS.exists(dir.c_str())) LittleFS.mkdir(dir.c_str());
    }

    File out = LittleFS.open(flashPath, "w");
    if (!out) { in.close(); return finish(false); }

    uint8_t buf[RNS_COPY_CHUNK];
    bool ok = true;
    while (in.available()) {
        size_t n = in.read(buf, sizeof(buf));
        if (n == 0) break;
        if (out.write(buf, n) != n) { ok = false; break; }
        copied += n;
        RNS::Utilities::OS::reset_watchdog();
    }
    in.close();
    out.close();
    if (!ok) LittleFS.remove(flashPath);
    return finish(ok);
}

bool copyFlashToSD(SDStore* sd, const char* flashPath, const char* sdPath) {
    unsigned long traceStart = PerfTrace::nowMs();
    size_t copied = 0;
    auto finish = [&](bool ok) -> bool {
        PerfTrace::logWrite("sd", "copy-flash-to-sd", sdPath, copied, ok,
                            PerfTrace::elapsedMs(traceStart));
        return ok;
    };

    if (!sd || !sd->isReady()) return finish(false);
    File in = LittleFS.open(flashPath, "r");
    if (!in || in.size() == 0) { if (in) in.close(); return finish(false); }

    String path = String(sdPath);
    int lastSlash = path.lastIndexOf('/');
    if (lastSlash > 0) {
        String dir = path.substring(0, lastSlash);
        if (!sd->ensureDir(dir.c_str())) { in.close(); return finish(false); }
    }

    String tmpPath = path + ".tmp";
    String bakPath = path + ".bak";
    SD.remove(tmpPath.c_str());
    File out = SD.open(tmpPath.c_str(), FILE_WRITE);
    if (!out) { in.close(); return finish(false); }

    uint8_t buf[RNS_COPY_CHUNK];
    bool ok = true;
    while (in.available()) {
        size_t n = in.read(buf, sizeof(buf));
        if (n == 0) break;
        if (out.write(buf, n) != n) { ok = false; break; }
        copied += n;
        RNS::Utilities::OS::reset_watchdog();
    }
    in.close();
    out.close();

    if (!ok) { SD.remove(tmpPath.c_str()); return finish(false); }
    if (SD.exists(sdPath)) {
        SD.remove(bakPath.c_str());
        SD.rename(sdPath, bakPath.c_str());
    }
    SD.remove(sdPath);
    if (!SD.rename(tmpPath.c_str(), sdPath)) {
        if (SD.exists(bakPath.c_str())) SD.rename(bakPath.c_str(), sdPath);
        SD.remove(tmpPath.c_str());
        return finish(false);
    }
    SD.remove(bakPath.c_str());
    return finish(true);
}

class LittleFSStreamImpl : public RNS::FileStreamImpl {
public:
    LittleFSStreamImpl(File&& f, const char* path, bool writeMode)
        : _f(std::move(f)), _writeMode(writeMode), _startMs(PerfTrace::nowMs()) {
        snprintf(_path, sizeof(_path), "%s", path ? path : "");
    }
    ~LittleFSStreamImpl() override { close(); }

protected:
    const char* name() override { return _f ? _f.name() : ""; }
    size_t size() override { return _f ? _f.size() : 0; }
    void close() override {
        if (!_f) return;
        _f.close();
        if (_writeMode && !_logged) {
            _logged = true;
            PerfTrace::logWrite("flash", "rns-stream", _path, _bytesWritten, !_writeError,
                                PerfTrace::elapsedMs(_startMs));
        }
    }

    size_t write(uint8_t byte) override {
        size_t written = _f ? _f.write(byte) : 0;
        _bytesWritten += written;
        if (written != 1) _writeError = true;
        return written;
    }
    size_t write(const uint8_t* buffer, size_t len) override {
        size_t written = _f ? _f.write(buffer, len) : 0;
        _bytesWritten += written;
        if (written != len) _writeError = true;
        return written;
    }

    int available() override { return _f ? _f.available() : 0; }
    int read() override { return _f ? _f.read() : -1; }
    int peek() override { return _f ? _f.peek() : -1; }
    void flush() override { if (_f) _f.flush(); }

private:
    File _f;
    char _path[80] = {};
    bool _writeMode = false;
    bool _logged = false;
    bool _writeError = false;
    size_t _bytesWritten = 0;
    unsigned long _startMs = 0;
};
}  // namespace

RNS::FileStream LittleFSFileSystem::open_file(const char* path, RNS::FileStream::MODE mode) {
    const char* openMode = "r";
    if (mode == RNS::FileStream::MODE_WRITE)  openMode = "w";
    if (mode == RNS::FileStream::MODE_APPEND) openMode = "a";
    if (mode != RNS::FileStream::MODE_READ) {
        String p(path);
        int lastSlash = p.lastIndexOf('/');
        if (lastSlash > 0 && !LittleFS.exists(p.substring(0, lastSlash).c_str())) {
            LittleFS.mkdir(p.substring(0, lastSlash).c_str());
        }
    }
    File f = LittleFS.open(path, openMode);
    if (!f) {
        if (mode != RNS::FileStream::MODE_READ) {
            PerfTrace::logWrite("flash", "rns-stream-open", path, 0, false, 0);
        }
        return {RNS::Type::NONE};
    }
    return RNS::FileStream(new LittleFSStreamImpl(std::move(f), path, mode != RNS::FileStream::MODE_READ));
}
bool LittleFSFileSystem::remove_file(const char* p) { return LittleFS.remove(p); }
bool LittleFSFileSystem::rename_file(const char* f, const char* t) { return LittleFS.rename(f, t); }
bool LittleFSFileSystem::directory_exists(const char* p) { return LittleFS.exists(p); }
bool LittleFSFileSystem::create_directory(const char* p) { return LittleFS.mkdir(p); }
bool LittleFSFileSystem::remove_directory(const char* p) { return LittleFS.rmdir(p); }

std::list<std::string> LittleFSFileSystem::list_directory(const char* p, Callbacks::DirectoryListing callback) {
    std::list<std::string> entries;
    File dir = LittleFS.open(p);
    if (!dir || !dir.isDirectory()) return entries;
    File f = dir.openNextFile();
    while (f) {
        const char* name = f.name();
        entries.push_back(name);
        if (callback) callback(name);
        f = dir.openNextFile();
    }
    return entries;
}

size_t LittleFSFileSystem::storage_size() { return LittleFS.totalBytes(); }
size_t LittleFSFileSystem::storage_available() { return LittleFS.totalBytes() - LittleFS.usedBytes(); }

bool ReticulumManager::begin(SX1262* radio, FlashStore* flash, bool loraEnabled) {
    _flash = flash;

    LittleFSFileSystem* fsImpl = new LittleFSFileSystem();
    RNS::FileSystem fs(fsImpl);
    fs.init();
    RNS::Utilities::OS::register_filesystem(fs);
    Serial.println("[RNS] Filesystem registered");

    // Endpoint mode learns paths live from announces/path requests. Upstream
    // Reticulum only restores destination_table in transport mode, so do not
    // resurrect stale endpoint path caches from SD or flash.
    if (LittleFS.exists("/destination_table")) LittleFS.remove("/destination_table");
    if (LittleFS.exists("/packet_hashlist")) LittleFS.remove("/packet_hashlist");

    // Restore known destination identities from SD if missing on flash.
    if (_sd && _sd->isReady()) {
        static const char* files[] = {"/known_destinations"};
        for (const char* name : files) {
            if (!LittleFS.exists(name)) {
                char sdPath[64];
                snprintf(sdPath, sizeof(sdPath), "%s%s", SD_PATH_TRANSPORT, name);
                if (copySDToFlash(sdPath, name)) {
                    File restored = LittleFS.open(name, "r");
                    size_t len = restored ? restored.size() : 0;
                    if (restored) restored.close();
                    Serial.printf("[RNS] Restored %s from SD (%d bytes)\n", name, (int)len);
                }
            }
        }
    }

    if (loraEnabled) {
        _loraImpl = new LoRaInterface(radio, "LoRa");
        _loraIface = _loraImpl;
        _loraIface.mode(RNS::Type::Interface::MODE_ROAMING);
        RNS::Transport::register_interface(_loraIface);
        if (!_loraImpl->start()) {
            Serial.println("[RNS] WARNING: LoRa interface failed to start");
        }
    } else {
        Serial.println("[RNS] LoRa interface disabled by config");
    }

    _reticulum = RNS::Reticulum();
    // Suppress verbose microReticulum logging — LOG_TRACE floods serial at 115200 baud,
    // blocking the CPU for hundreds of ms. Change to LOG_TRACE or LOG_DEBUG for protocol debugging.
    RNS::loglevel(RNS::LOG_WARNING);
    RNS::Reticulum::transport_enabled(false);
    RNS::Reticulum::probe_destination_enabled(true);
    RNS::Transport::path_table_maxsize(256);
    RNS::Transport::announce_table_maxsize(128);
    _reticulum.start();
    Serial.println("[RNS] Reticulum started (Endpoint)");

    // Layer 1: Transport-level announce filter — runs BEFORE Ed25519 verify
    RNS::Transport::set_filter_packet_callback([](const RNS::Packet& packet) -> bool {
        if (packet.packet_type() != RNS::Type::Packet::ANNOUNCE) return true;

        unsigned long now = millis();

        // Rate limit window (per-second)
        static unsigned long windowStart = 0;
        static unsigned int count = 0;
        if (now - windowStart >= 1000) { windowStart = now; count = 0; }

        if (packet.context() == RNS::Type::Packet::PATH_RESPONSE) return true;

        // Adaptive rate: tighter during first 60s boot flood, then normal
        unsigned int maxRate = (now < 60000) ? 3 : RATDECK_MAX_ANNOUNCES_PER_SEC;
        if (++count > maxRate) { ReticulumManager::_announceFilterCount++; return false; }

        return true;
    });

    // Load persisted known destinations so Identity::recall() works
    // immediately after reboot for previously-seen nodes.
    RNS::Identity::load_known_destinations();

    if (!loadOrCreateIdentity()) {
        Serial.println("[RNS] ERROR: Identity creation failed!");
        return false;
    }

    _destination = RNS::Destination(
        _identity,
        RNS::Type::Destination::IN,
        RNS::Type::Destination::SINGLE,
        "lxmf",
        "delivery"
    );
    _destination.set_proof_strategy(RNS::Type::Destination::PROVE_ALL);
    _destination.accepts_links(true);

    _transportActive = true;
    Serial.println("[RNS] Endpoint active");
    return true;
}

bool ReticulumManager::loadOrCreateIdentity() {
    // Tier 1: Flash (LittleFS)
    if (_flash->exists(PATH_IDENTITY)) {
        RNS::Bytes keyData;
        if (RNS::Utilities::OS::read_file(PATH_IDENTITY, keyData) > 0) {
            _identity = RNS::Identity(false);
            if (_identity.load_private_key(keyData)) {
                Serial.printf("[RNS] Identity loaded from flash: %s\n", _identity.hexhash().c_str());
                saveIdentityToAll(keyData);
                return true;
            }
        }
    }

    if (_sd && _sd->isReady() && _sd->exists(SD_PATH_IDENTITY)) {
        Serial.println("[RNS] SD identity backup present; automatic import disabled");
    }

    // Tier 2: NVS (ESP32 Preferences — always available)
    {
        Preferences prefs;
        if (prefs.begin("ratpager_id", true)) {
            size_t keyLen = prefs.getBytesLength("privkey");
            if (keyLen > 0 && keyLen <= 128) {
                uint8_t keyBuf[128];
                prefs.getBytes("privkey", keyBuf, keyLen);
                prefs.end();
                RNS::Bytes keyData(keyBuf, keyLen);
                _identity = RNS::Identity(false);
                if (_identity.load_private_key(keyData)) {
                    Serial.printf("[RNS] Identity restored from NVS: %s\n", _identity.hexhash().c_str());
                    saveIdentityToAll(keyData);
                    return true;
                }
            } else {
                prefs.end();
            }
        }
    }

    // No identity found anywhere — create new
    _identity = RNS::Identity();
    Serial.printf("[RNS] New identity created: %s\n", _identity.hexhash().c_str());

    RNS::Bytes privKey = _identity.get_private_key();
    if (privKey.size() > 0) {
        saveIdentityToAll(privKey);
    }
    return true;
}

void ReticulumManager::saveIdentityToAll(const RNS::Bytes& keyData) {
    // Flash
    _flash->writeAtomic(PATH_IDENTITY, keyData.data(), keyData.size());
    // NVS (always available, survives flash/SD failures)
    Preferences prefs;
    if (prefs.begin("ratpager_id", false)) {
        prefs.putBytes("privkey", keyData.data(), keyData.size());
        prefs.end();
        Serial.println("[RNS] Identity saved to NVS");
    }
}

void ReticulumManager::loop() {
    if (!_transportActive) return;
    _reticulum.loop();
    unsigned long now = millis();
    if (now - _lastPersist >= PATH_PERSIST_INTERVAL_MS) {
        _lastPersist = now;
        persistData();
    }
}

// Synchronous persist — one cycle per call to spread I/O across intervals.
// Runs on core 1 (main loop) to avoid data races with microReticulum's
// single-threaded transport state. rsPager is an endpoint, not a transport
// node, so path tables are intentionally not persisted across boots.
void ReticulumManager::persistData() {
    unsigned long start = PerfTrace::nowMs();
    size_t bytes = 0;
    bool sdOk = false;
    switch (_persistCycle) {
        case 0:
            RNS::Identity::persist_data();
            break;
        case 1:
            if (_sd && _sd->isReady()) {
                static const char* files[] = {"/known_destinations"};
                for (const char* name : files) {
                    File f = LittleFS.open(name, "r");
                    if (f && f.size() > 0) {
                        bytes += f.size();
                        f.close();
                        char sdPath[64];
                        snprintf(sdPath, sizeof(sdPath), "%s%s", SD_PATH_TRANSPORT, name);
                        sdOk = copyFlashToSD(_sd, name, sdPath) || sdOk;
                    } else {
                        if (f) f.close();
                    }
                }
            }
            break;
    }
    unsigned long dur = PerfTrace::elapsedMs(start);
    if (PerfTrace::shouldLog(dur, RSPAGER_PERF_PERSIST_TRACE_MS) ||
        (bytes > 0 && _sd && _sd->isReady() && !sdOk)) {
        RSPAGER_PERF_PRINTF("[PERF] RNS persistData: cycle=%u bytes=%u sd_ok=%d total=%lums\n",
                            (unsigned)_persistCycle, (unsigned)bytes, sdOk ? 1 : 0, dur);
    }
    Serial.printf("[PERSIST] Cycle %d done (%lums)\n", _persistCycle, dur);
    if (dur > 500) {
        Serial.printf("[PERSIST] WARNING: Cycle %d blocked for %lums!\n", _persistCycle, dur);
    }
    _persistCycle = (_persistCycle + 1) % 2;
}

String ReticulumManager::identityHash() const {
    if (!_identity) return "unknown";
    std::string hex = _identity.hexhash();
    if (hex.length() >= 12) {
        return String((hex.substr(0, 4) + ":" + hex.substr(4, 4) + ":" + hex.substr(8, 4)).c_str());
    }
    return String(hex.c_str());
}

String ReticulumManager::destinationHashHex() const {
    if (!_destination) return "unknown";
    return String(_destination.hash().toHex().c_str());
}

String ReticulumManager::destinationHashStr() const {
    if (!_destination) return "unknown";
    std::string hex = _destination.hash().toHex();
    if (hex.length() >= 12) {
        return String((hex.substr(0, 4) + ":" + hex.substr(4, 4) + ":" + hex.substr(8, 4)).c_str());
    }
    return String(hex.c_str());
}

size_t ReticulumManager::pathCount() const { return _reticulum.get_path_table().size(); }
size_t ReticulumManager::linkCount() const { return _reticulum.get_link_count(); }

void ReticulumManager::announce(const RNS::Bytes& appData) {
    if (!_transportActive) return;
    Serial.println("[ANNOUNCE-TX] === Starting ===");
    Serial.printf("[ANNOUNCE-TX] dest_hash:     %s\n", _destination.hash().toHex().c_str());
    Serial.printf("[ANNOUNCE-TX] identity_hash: %s\n", _identity.hexhash().c_str());
    Serial.printf("[ANNOUNCE-TX] app_data size: %d bytes\n", (int)appData.size());
    if (appData.size() > 0) {
        Serial.printf("[ANNOUNCE-TX] app_data hex:  %s\n", appData.toHex().c_str());
    }
    // Log registered interfaces
    auto& ifaces = RNS::Transport::get_interfaces();
    Serial.printf("[ANNOUNCE-TX] registered interfaces: %d\n", (int)ifaces.size());
    for (const auto& [hash, iface] : ifaces) {
        Serial.printf("[ANNOUNCE-TX]   iface: %s OUT=%d online=%d mode=%d\n",
            iface.toString().c_str(), iface.OUT(), iface.online(), (int)iface.mode());
    }
    unsigned long startMs = millis();
    _destination.announce(appData);
    _lastAnnounceTime = millis();
    Serial.printf("[ANNOUNCE-TX] === Complete === (%lu ms)\n", millis() - startMs);
}
