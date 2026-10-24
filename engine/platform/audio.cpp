#include "platform/audio.hpp"

#include <SDL3/SDL.h>

#include <cstdio>

#include "render/vorbis_wrapper.h"

namespace openday::platform {

struct AudioPlayer::Music {
    void* file = nullptr;
    SDL_AudioStream* stream = nullptr;
    int channels = 2;
    bool loop = false;
    bool ended = false;

    ~Music() {
        if (stream) SDL_DestroyAudioStream(stream);
        if (file) openday_vorbis_close(file);
    }
};

AudioPlayer::AudioPlayer() {
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "no audio: %s\n", SDL_GetError());
        return;
    }
    device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!device_) std::fprintf(stderr, "no audio: %s\n", SDL_GetError());
}

AudioPlayer::~AudioPlayer() {
    music_.reset();
    for (SDL_AudioStream* s : streams_) SDL_DestroyAudioStream(s);
    if (device_) SDL_CloseAudioDevice(device_);
}

void AudioPlayer::play(const formats::SmkAudio& audio) {
    if (!device_ || !soundEnabled || audio.pcm.empty()) return;
    const SDL_AudioSpec spec{audio.bitsPerSample == 16 ? SDL_AUDIO_S16LE : SDL_AUDIO_U8, audio.channels,
                             static_cast<int>(audio.sampleRate)};
    SDL_AudioStream* s = SDL_CreateAudioStream(&spec, nullptr);
    if (!s) return;
    if (!SDL_BindAudioStream(device_, s)) {
        SDL_DestroyAudioStream(s);
        return;
    }
    SDL_PutAudioStreamData(s, audio.pcm.data(), static_cast<int>(audio.pcm.size()));
    SDL_FlushAudioStream(s);
    streams_.push_back(s);
}

void AudioPlayer::playWav(const std::filesystem::path& file) {
    if (!device_ || !soundEnabled) return;
    const std::string key = file.string();
    auto it = wavs_.find(key);
    if (it == wavs_.end()) {
        formats::SmkAudio a;
        SDL_AudioSpec spec{};
        Uint8* buf = nullptr;
        Uint32 len = 0;
        if (SDL_LoadWAV(key.c_str(), &spec, &buf, &len)) {
            a.sampleRate = static_cast<std::uint32_t>(spec.freq);
            a.channels = spec.channels;
            a.bitsPerSample = SDL_AUDIO_BITSIZE(spec.format);
            a.pcm.assign(buf, buf + len);
            SDL_free(buf);
            if (spec.format != SDL_AUDIO_U8 && spec.format != SDL_AUDIO_S16LE) a.pcm.clear(); // unsupported
        }
        it = wavs_.emplace(key, std::move(a)).first;
    }
    play(it->second);
}

void AudioPlayer::stopSounds() {
    for (SDL_AudioStream* s : streams_) SDL_DestroyAudioStream(s);
    streams_.clear();
}

void AudioPlayer::playMusic(const std::filesystem::path& ogg, bool loop) {
    stopMusic();
    if (!device_) return;
    int channels = 0, rate = 0;
    void* f = openday_vorbis_open(ogg.string().c_str(), &channels, &rate);
    if (!f) return;
    auto m = std::make_unique<Music>();
    m->file = f;
    m->channels = channels;
    m->loop = loop;
    const SDL_AudioSpec spec{SDL_AUDIO_S16, channels, rate};
    m->stream = SDL_CreateAudioStream(&spec, nullptr);
    if (!m->stream || !SDL_BindAudioStream(device_, m->stream)) return;
    music_ = std::move(m);
    update();
}

void AudioPlayer::stopMusic() { music_.reset(); }

bool AudioPlayer::musicPlaying() const { return music_ && !(music_->ended && SDL_GetAudioStreamQueued(music_->stream) == 0); }

void AudioPlayer::update() {
    std::erase_if(streams_, [](SDL_AudioStream* s) {
        if (SDL_GetAudioStreamQueued(s) > 0) return false;
        SDL_DestroyAudioStream(s);
        return true;
    });
    if (!music_ || music_->ended) return;
    // Keep about half a second queued.
    short buffer[8192];
    while (SDL_GetAudioStreamQueued(music_->stream) < 96000) {
        const int frames =
            openday_vorbis_read(music_->file, music_->channels, buffer, static_cast<int>(std::size(buffer)));
        if (frames <= 0) {
            if (music_->loop && openday_vorbis_rewind(music_->file)) continue;
            music_->ended = true;
            SDL_FlushAudioStream(music_->stream);
            break;
        }
        SDL_PutAudioStreamData(music_->stream, buffer, frames * music_->channels * static_cast<int>(sizeof(short)));
    }
}

} // namespace openday::platform
