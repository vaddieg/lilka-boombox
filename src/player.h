// SD music playback on top of ESP32-audioI2S (v4.x API).
//
// Non-recursive folder listing: openFolder() lists one directory (subfolders +
// audio files). The player only plays files from the folder it was started in;
// next/prev stay inside that folder.

#pragma once

#include <Arduino.h>
#include <SD.h>
#include <Audio.h>
#include <mutex>
#include <vector>

class MusicPlayer {
public:
    struct Entry {
        String name;
        bool isDir = false;
    };

    enum Event {
        EV_NONE = 0,
        EV_TRACK_END,   // current file finished, auto-advance pending
    };

    bool begin();

    /// List one directory (non-recursive). Folders first, then audio files, both sorted.
    /// Returns false if the directory cannot be opened.
    bool openFolder(const String& path);
    const String& folder() const { return m_folder; }
    size_t entryCount() const { return m_entries.size(); }
    const Entry& entry(size_t i) const { return m_entries[i]; }
    /// Full path (SD-relative, e.g. "/music/track.mp3") of entry i.
    String entryPath(size_t i) const;

    /// Play entry i (must be a file). Returns false on failure.
    bool playEntry(size_t i);

    bool nextTrack();
    bool prevTrack();
    /// Shuffle toggle: randomly reorders the current folder's files among
    /// themselves (folders keep their place); pressing again restores the
    /// original sorted order. The current track stays selected.
    void toggleShuffle();
    bool isShuffled() const { return m_shuffled; }
    void stop();
    void togglePause();

    bool isPlaying() const { return m_playing; }
    bool isPaused() const { return m_paused; }
    /// Name of the current track ("" when nothing is playing).
    String entryName() const {
        if (!m_playing || m_current >= m_entries.size()) return "";
        return m_entries[m_current].name;
    }
    /// True once per finished track (cleared by next play/stop).
    Event pollEvent();

    /// Call once per main-loop iteration.
    void loop();

    /// Playback time in seconds (0 if unknown).
    uint32_t positionSec() { return (m_audioOn && m_playing) ? aud.getAudioCurrentTime() : 0; }
    uint32_t durationSec() { return (m_audioOn && m_playing) ? aud.getAudioFileDuration() : 0; }

    /// App volume 0..100, mapped onto the library's 21 volume steps.
    void setVolume100(int v);
    int volume100() const { return m_vol100; }

    /// Spectrum: 15 bands, 0..255 (library-smoothed bars + peak hold).
    static constexpr size_t SPECTRUM_BANDS = 15;
    /// Thread-safe: callable from the main loop while the audio task updates.
    void getSpectrum(uint8_t* bars, uint8_t* peaks = nullptr);

    /// --- Internet radio (stage 3) ------------------------------------------
    /// (Re)build the station list: "*.url" files in /webradio on the SD root
    /// (name = filename without extension, URL = trimmed file content). Falls
    /// back to the built-in list when the folder is missing or empty.
    void loadStations();
    size_t stationCount() const { return m_stations.size(); }
    const String& stationName(size_t i) const { return m_stations[i].name; }
    size_t currentStation() const { return m_station; }
    bool isRadio() const { return m_radio; }
    /// Latest stream title (icy-name / streamtitle), station name as fallback.
    const String& streamTitle() const { return m_streamTitle; }

    /// Read WiFi credentials stored by Keira OS (NVS namespace "network":
    /// "last_ssid" + "<hash(ssid)>_pw"). Returns false if not present.
    bool wifiCreds(String& ssid, String& pass) const;
    /// Connect to the given network, waiting up to timeoutMs. WiFi must be off
    /// before the call (power saving: we only bring it up for radio mode).
    static bool connectWiFi(const String& ssid, const String& pass, int timeoutMs);
    /// Turn WiFi off completely (same procedure as Keira's NetworkService).
    static void wifiOff();

    /// Start streaming station i (WiFi must already be connected).
    bool playStation(size_t i);
    /// Stop the stream and power down WiFi.
    void stopRadio();

private:
    struct Station {
        String name;
        String url;
    };
    struct BuiltinStation {
        const char* name;
        const char* url;
    };
    static const BuiltinStation kBuiltinStations[]; // used when /webradio is absent

    bool playPath(const String& full);
    bool playAtOffset(int delta);
    void restoreOrder();
    static void onAudioEvent(Audio::msg_t m);

    Audio aud;
    // radio state
    std::vector<Station> m_stations;
    size_t m_station = 0;
    bool m_radio = false;
    String m_streamTitle;
    String m_folder;
    std::vector<Entry> m_entries;
    size_t m_current = 0;
    bool m_playing = false;
    std::vector<Entry> m_origEntries; // original sorted order (while shuffled)
    bool m_shuffled = false;
    bool m_paused = false;
    bool m_trackEnd = false;
    int m_vol100 = 70;
    int m_volSteps = 21;
    // Audio engine (ESP32-audioI2S 4.0.0). Was temporarily disabled while we
    // debugged the shared-SPI hang; re-enabled now that SD works.
    bool m_audioOn = true;
    uint8_t m_spectBars[SPECTRUM_BANDS] = {};
    uint8_t m_spectPeaks[SPECTRUM_BANDS] = {};
    std::mutex m_spectMux; // audio task writes, main loop reads
    static MusicPlayer* s_self; // for the static audio event callback
};
