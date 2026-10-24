// Sound output through SDL3: sound effects (animation clips, WAV files) and
// streamed music (Ogg Vorbis, the GOG replacement for the CD tracks; see
// docs/mechanics/interface.md "Sounds and music").
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "formats/smk.hpp"

struct SDL_AudioStream;

namespace openday::platform {

// Plays sounds; every sound gets its own stream on one device, and SDL
// mixes them. Works silently when there is no audio device.
class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    bool available() const { return device_ != 0; }
    bool soundEnabled = true;

    void play(const formats::SmkAudio& audio);
    // Loads SOUNDS\<file> once and plays it; missing files are ignored.
    void playWav(const std::filesystem::path& file);
    // Stops every sound effect (not the music).
    void stopSounds();

    // Streams an Ogg Vorbis file, replacing the current music; loops if asked.
    void playMusic(const std::filesystem::path& ogg, bool loop);
    void stopMusic();
    bool musicPlaying() const;

    // Feeds the music stream and releases finished sounds; call once per frame.
    void update();

private:
    struct Music;
    std::uint32_t device_ = 0;
    std::vector<SDL_AudioStream*> streams_;
    std::map<std::string, formats::SmkAudio> wavs_;
    std::unique_ptr<Music> music_;
};

} // namespace openday::platform
