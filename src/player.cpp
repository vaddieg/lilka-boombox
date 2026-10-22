#include "player.h"
#include "hw.h"

#include <WiFi.h>
#include <Preferences.h>
#include <algorithm>

const MusicPlayer::BuiltinStation MusicPlayer::kBuiltinStations[] = {
    {"3FM (NL)", "http://icecast.omroep.nl/3fm-bb-mp3"},
    {"Jazz (ofr.fm)", "http://air.ofr.fm:8008/jazz/mp3/128"},
    {"WDR2 (DE)", "https://wdr-wdr2-ruhrgebiet.icecastssl.wdr.de/wdr/wdr2/ruhrgebiet/mp3/128/stream.mp3"},
    {"Antenne Oldies (DE)", "http://mp3channels.webradio.antenne.de:80/oldies-but-goldies"},
};

namespace {

const char* const kAudioExts[] = {
    ".mp3", ".flac", ".wav", ".ogg", ".opus", ".m4a", ".aac", ".wma", ".mid"
};

// Keira OS NetworkService password key: djb2-style hash of the SSID,
// lower 32 bits as 8 hex chars (see keira/src/services/network/network.cpp).
String keiraSsidHash(const String& s) {
    uint64_t h = 0;
    for (unsigned i = 0; i < s.length(); i++)
        h = (h << 5) - h + (unsigned char)s[i];
    char buf[9];
    snprintf(buf, sizeof(buf), "%08x", (unsigned int)h);
    return String(buf);
}

bool isAudioFile(const String& name) {
    String n = name;
    n.toLowerCase(); // in-place on this core
    for (const char* ext : kAudioExts)
        if (n.endsWith(ext)) return true;
    return false;
}

bool nameLess(const String& a, const String& b) {
    String x = a, y = b;
    x.toLowerCase();
    y.toLowerCase();
    return x < y;
}

} // namespace

MusicPlayer* MusicPlayer::s_self = nullptr;

bool MusicPlayer::begin() {
    s_self = this;
    if (!m_audioOn) {
        Serial.println("player: audio engine disabled (SD/UI test mode)");
        return true;
    }
    Audio::audio_info_callback = onAudioEvent;
    Serial.println("[player] i2s setPinout...");
    if (!aud.setPinout(hw::PIN_I2S_BCLK, hw::PIN_I2S_LRCK, hw::PIN_I2S_DOUT)) {
        Serial.println("I2S pinout failed");
        return false;
    }
    aud.settings.SPECTRUM = true; // off by default; needed for evt_spectrum events
    Serial.println("[player] i2s ok");
    m_volSteps = aud.getVolumeSteps();
    setVolume100(m_vol100);
    Serial.printf("[player] ready, %u volume steps\n", m_volSteps);
    return true;
}

void MusicPlayer::onAudioEvent(Audio::msg_t m) {
    if (!s_self) return;
    if (m.e == Audio::evt_eof) {
        s_self->m_trackEnd = true;
    } else if (m.e == Audio::evt_streamtitle || m.e == Audio::evt_name) {
        // song title overrides the station name as soon as the server sends it
        if (m.msg && m.msg[0]) s_self->m_streamTitle = m.msg;
    } else if (m.e == Audio::evt_spectrum && m.vec1.size() >= MusicPlayer::SPECTRUM_BANDS) {
        // note: the library pushes NUM_BANDS=16 elements (band 15 is the unused
        // 20 kHz band) — accept >= and take the first 15
        std::lock_guard<std::mutex> lk(s_self->m_spectMux);
        for (size_t i = 0; i < MusicPlayer::SPECTRUM_BANDS; i++) {
            s_self->m_spectBars[i] = (uint8_t)m.vec1[i];
            s_self->m_spectPeaks[i] = (m.vec2.size() >= MusicPlayer::SPECTRUM_BANDS) ? (uint8_t)m.vec2[i] : 0;
        }
    }
}

void MusicPlayer::getSpectrum(uint8_t* bars, uint8_t* peaks) {
    std::lock_guard<std::mutex> lk(m_spectMux);
    memcpy(bars, m_spectBars, SPECTRUM_BANDS);
    if (peaks) memcpy(peaks, m_spectPeaks, SPECTRUM_BANDS);
}

bool MusicPlayer::openFolder(const String& path) {
    String folder = path;
    if (folder.length() > 1 && folder.endsWith("/")) folder.remove(folder.length() - 1);

    File dir = SD.open(folder);
    if (!dir || !dir.isDirectory()) {
        Serial.println("openFolder failed: " + folder);
        return false;
    }

    m_entries.clear();
    File f = dir.openNextFile();
    while (f) {
        if (f.isDirectory()) {
            m_entries.push_back({String(f.name()), true});
        } else if (isAudioFile(f.name())) {
            m_entries.push_back({String(f.name()), false});
        }
        f = dir.openNextFile();
    }
    dir.close();

    // folders first, then files; case-insensitive sort within groups
    std::stable_sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) {
        if (a.isDir != b.isDir) return a.isDir > b.isDir;
        return nameLess(a.name, b.name);
    });

    m_folder = folder;
    m_current = 0;
    m_shuffled = false;
    m_origEntries.clear();
    return true;
}

String MusicPlayer::entryPath(size_t i) const {
    return m_folder + "/" + m_entries[i].name;
}

bool MusicPlayer::playEntry(size_t i) {
    if (i >= m_entries.size() || m_entries[i].isDir) return false;
    return playPath(entryPath(i));
}

bool MusicPlayer::playPath(const String& full) {
    m_trackEnd = false;
    m_paused = false;
    if (m_audioOn && !aud.connecttoFS(SD, full.c_str())) {
        Serial.println("play failed: " + full);
        m_playing = false;
        return false;
    }
    m_playing = true;
    Serial.println("[player] playing: " + full);
    size_t idx = 0;
    for (size_t i = 0; i < m_entries.size(); i++) {
        if (!m_entries[i].isDir && entryPath(i) == full) { idx = i; break; }
    }
    m_current = idx;
    return true;
}

bool MusicPlayer::playAtOffset(int delta) {
    if (m_entries.empty()) return false;
    size_t n = m_entries.size();
    size_t i = m_current;
    for (int step = 0; step < (int)n; step++) {
        i = (i + n + delta) % n;
        if (!m_entries[i].isDir) return playEntry(i);
    }
    return false; // no audio files in folder
}

bool MusicPlayer::nextTrack() { return playAtOffset(+1); }
bool MusicPlayer::prevTrack() { return playAtOffset(-1); }

void MusicPlayer::restoreOrder() {
    if (!m_shuffled) return;
    String cur = (m_current < m_entries.size()) ? m_entries[m_current].name : "";
    m_entries = m_origEntries;
    m_origEntries.clear();
    m_shuffled = false;
    size_t idx = 0;
    for (size_t i = 0; i < m_entries.size(); i++)
        if (!m_entries[i].isDir && m_entries[i].name == cur) { idx = i; break; }
    m_current = idx;
}

void MusicPlayer::toggleShuffle() {
    if (m_shuffled) { restoreOrder(); return; }

    size_t files = 0;
    for (const auto& e : m_entries) if (!e.isDir) files++;
    if (files < 2) return; // nothing to shuffle

    // capture the current track's name BEFORE reordering — m_current points
    // into m_entries, which the shuffle below rewrites
    String cur = (m_current < m_entries.size()) ? m_entries[m_current].name : "";

    m_origEntries = m_entries;
    // Fisher-Yates over the audio files only; folders keep their places
    std::vector<size_t> fpos;
    fpos.reserve(files);
    for (size_t i = 0; i < m_entries.size(); i++)
        if (!m_entries[i].isDir) fpos.push_back(i);
    std::vector<Entry> pool;
    pool.reserve(files);
    for (size_t i : fpos) pool.push_back(m_entries[i]);
    for (size_t i = pool.size() - 1; i > 0; i--) {
        size_t j = random(i + 1);
        std::swap(pool[i], pool[j]);
    }
    for (size_t k = 0; k < fpos.size(); k++) m_entries[fpos[k]] = pool[k];

    // reindex the current track in the new order
    size_t idx = 0;
    for (size_t i = 0; i < m_entries.size(); i++)
        if (!m_entries[i].isDir && m_entries[i].name == cur) { idx = i; break; }
    m_current = idx;
    m_shuffled = true;
}

void MusicPlayer::stop() {
    if (m_audioOn) aud.stopSong();
    m_playing = false;
    m_paused = false;
    m_trackEnd = false;
    restoreOrder(); // back to the browser: show the folder in its normal order
    {
        std::lock_guard<std::mutex> lk(m_spectMux);
        memset(m_spectBars, 0, sizeof(m_spectBars));
        memset(m_spectPeaks, 0, sizeof(m_spectPeaks));
    }
}

void MusicPlayer::togglePause() {
    if (!m_playing) return;
    if (m_audioOn) aud.pauseResume();
    m_paused = !m_paused;
}

MusicPlayer::Event MusicPlayer::pollEvent() {
    if (m_trackEnd) {
        m_trackEnd = false;
        return EV_TRACK_END;
    }
    return EV_NONE;
}

void MusicPlayer::loop() {
    if (m_audioOn) aud.loop();
}

void MusicPlayer::setVolume100(int v) {
    m_vol100 = constrain(v, 0, 100);
    if (!m_audioOn) return;
    int steps = constrain(m_volSteps, 1, 255);
    aud.setVolume((int)((uint32_t)m_vol100 * steps / 100));
}

// ---------------------------------------------------------------- radio

void MusicPlayer::loadStations() {
    m_stations.clear();

    // User stations: "*.url" text files in /webradio on the SD root,
    // station name = filename without the extension.
    File dir = SD.open("/webradio");
    if (dir && dir.isDirectory()) {
        File f = dir.openNextFile();
        while (f) {
            String fname = f.name(); // File::name() is const char* on this core
            if (!f.isDirectory() && fname.endsWith(".url")) {
                String content;
                while (f.available() && content.length() < 512) content += (char)f.read();
                content.trim();
                int nl = content.indexOf('\n');
                if (nl >= 0) content = content.substring(0, nl); // first line only
                content.trim();
                if (content.length()) {
                    String nm = fname.substring(0, fname.length() - 4); // strip ".url"
                    m_stations.push_back({nm, content});
                }
            }
            f = dir.openNextFile();
        }
        dir.close();
        if (m_stations.size()) {
            std::stable_sort(m_stations.begin(), m_stations.end(),
                             [](const Station& a, const Station& b) { return nameLess(a.name, b.name); });
            Serial.printf("[radio] %u user station(s) from /webradio\n", (unsigned)m_stations.size());
            return;
        }
        Serial.println("[radio] /webradio has no valid .url files");
    }

    // Fallback: built-in list
    for (const auto& s : kBuiltinStations) m_stations.push_back({String(s.name), String(s.url)});
    Serial.printf("[radio] using %u built-in station(s)\n", (unsigned)m_stations.size());
}

bool MusicPlayer::wifiCreds(String& ssid, String& pass) const {
    // Keira OS stores: namespace "network", key "last_ssid",
    // and "<hash(ssid)>_pw" for the password of that network.
    Preferences prefs;
    prefs.begin("network", true);
    ssid = prefs.getString("last_ssid", "");
    if (ssid.length()) pass = prefs.getString((keiraSsidHash(ssid) + "_pw").c_str(), "");
    prefs.end();
    return ssid.length() && pass.length();
}

bool MusicPlayer::connectWiFi(const String& ssid, const String& pass, int timeoutMs) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.printf("[radio] wifi: connecting to %s ...", ssid.c_str());
    uint32_t t0 = millis();
    while (millis() - t0 < (uint32_t)timeoutMs) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf(" ok, ip %s, rssi %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
            return true;
        }
        delay(100);
    }
    Serial.println(" timeout");
    return false;
}

void MusicPlayer::wifiOff() {
    // same procedure as Keira's NetworkService when WiFi is disabled
    WiFi.disconnect(true, true); // erase RAM config, skip events
    WiFi.mode(WIFI_OFF);
    Serial.println("[radio] wifi off");
}

bool MusicPlayer::playStation(size_t i) {
    if (i >= m_stations.size()) return false;
    m_station = i;
    m_radio = true;
    m_paused = false;
    m_trackEnd = false;
    m_streamTitle = m_stations[i].name; // shown until the server sends a title
    Serial.printf("[radio] station %u: %s\n", (unsigned)i, m_stations[i].url.c_str());
    if (!m_audioOn || !aud.connecttohost(m_stations[i].url.c_str())) {
        m_radio = false;
        m_playing = false;
        return false;
    }
    m_playing = true;
    return true;
}

void MusicPlayer::stopRadio() {
    if (m_audioOn) aud.stopSong();
    m_radio = false;
    m_playing = false;
    m_paused = false;
    m_trackEnd = false;
    m_streamTitle = "";
    wifiOff();
}
