#include "app/app.hpp"

#include <algorithm>
#include <cctype>
#include <random>

#include "data/sqlite_db.hpp"
#include "formats/bmp_palette.hpp"
#include "formats/ff.hpp"
#include "formats/game_path.hpp"
#include "formats/pe_resources.hpp"
#include "info/encyclopedia.hpp"

namespace openday::app {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

std::uint8_t nearestColor(const formats::Palette& p, int r, int g, int b) {
    std::size_t best = 0;
    long bestDist = -1;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const long dr = p[i].r - r, dg = p[i].g - g, db = p[i].b - b;
        const long d = dr * dr + dg * dg + db * db;
        if (bestDist < 0 || d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return static_cast<std::uint8_t>(best);
}

std::string GameData::text(int id) const {
    const auto it = strings.find(id);
    return it == strings.end() ? std::string() : it->second;
}

fs::path GameData::scenario(const std::string& name) const {
    const auto it = scenarios.find(lower(fs::path(name).stem().string()));
    return it == scenarios.end() ? fs::path() : it->second;
}

fs::path GameData::file(const std::string& relative) const { return formats::findGameFile(gameDir, relative); }

GameData GameData::load(const fs::path& gameDir, const fs::path& dataDir) {
    GameData d;
    d.gameDir = gameDir;
    d.dataDir = dataDir;
    fs::path global;
    for (const auto& e : fs::recursive_directory_iterator(dataDir)) {
        if (!e.is_regular_file() || lower(e.path().extension().string()) != ".sqlite") continue;
        const std::string stem = lower(e.path().stem().string());
        if (stem == "global") global = e.path();
        else if (stem.size() == 2 && stem[0] == 'c' && stem[1] >= '1' && stem[1] <= '4')
            d.colonies[stem[1] - '0'] = e.path();
        else if (!d.scenarios.count(stem) && data::SqliteDb(e.path()).hasTable("Map")) d.scenarios[stem] = e.path();
    }
    if (global.empty()) throw data::DbError("GLOBAL.sqlite not found in " + std::string(reinterpret_cast<const char*>(dataDir.u8string().c_str())));
    d.global = global;
    d.rules = game::loadGlobalRules(data::SqliteDb(global));
    d.sprites = game::SpriteLibrary::fromGameDir(gameDir);
    d.palette = formats::loadBmpPalette(d.file("INTERF/PAL.BMP"));
    d.cycle = formats::PalCycle::fromFile(d.file("INTERF/PALCYCLE.PAL"));
    const fs::path ff = d.file("ANMSUNIT/ANMSUNIT.FF");
    if (fs::exists(ff))
        d.clips = std::make_unique<render::AnimationCache>(std::make_shared<const formats::FfArchive>(formats::FfArchive::open(ff)));
    const fs::path dll = d.file("EXE/LIBDAYEN.DLL");
    if (fs::exists(dll)) d.strings = formats::loadStringTable(dll);

    // The skirmish catalog.
    for (const auto& e : fs::recursive_directory_iterator(dataDir)) {
        if (!e.is_regular_file() || lower(e.path().filename().string()) != "skirmish.sqlite") continue;
        data::SqliteDb db(e.path());
        db.forEach("SELECT TitleEnglish, provincenameEnglish, descriptionEnglish, \"One Off Deployment\", Size, FileName "
                   "FROM \"Scenario Info\" ORDER BY Counter",
                   [&](const data::Row& r) {
                       ScenarioEntry s{r.text(0), r.text(1), r.text(2), static_cast<int>(r.integer(3)),
                                       static_cast<int>(r.integer(4)), d.scenario(r.text(5))};
                       if (!s.database.empty()) d.skirmish.push_back(std::move(s));
                   });
        break;
    }
    return d;
}

App::App(GameData& data, platform::AudioPlayer& audio)
    : data_(data), audio_(audio), standard_(std::make_unique<render::Font>(render::FontFace::Regular, 14)),
      large_(std::make_unique<render::Font>(render::FontFace::Regular, 17)) {
    const fs::path atlas = data.file("INTERF/640X480.SMK");
    if (fs::exists(atlas)) atlas_ = ui::InterfaceAtlas::load(atlas, atlasElements());
    ui_.atlas = &atlas_;
    ui_.standard = standard_.get();
    ui_.large = large_.get();
    ui_.white = nearestColor(data.palette, 255, 255, 255);
    ui_.black = nearestColor(data.palette, 0, 0, 0);
    ui_.grey = nearestColor(data.palette, 128, 128, 128);
    ui_.highlight = nearestColor(data.palette, 40, 60, 140);
}

App::~App() = default;

void App::push(std::unique_ptr<Screen> screen) { pending_.push_back({Change::Kind::Push, std::move(screen)}); }
void App::replace(std::unique_ptr<Screen> screen) { pending_.push_back({Change::Kind::Replace, std::move(screen)}); }
void App::pop() { pending_.push_back({Change::Kind::Pop, nullptr}); }
void App::reset(std::unique_ptr<Screen> screen) { pending_.push_back({Change::Kind::Reset, std::move(screen)}); }

const ui::UiContext& App::ui(int resolution) {
    if (resolution != 1) return ui_;
    if (!largeLoaded_) {
        largeLoaded_ = true;
        uiLarge_ = ui_;
        const fs::path atlas = data_.file("INTERF/1024X768.SMK");
        if (fs::exists(atlas)) {
            atlasLarge_ = ui::InterfaceAtlas::load(atlas, atlasElements(1));
            uiLarge_.atlas = &atlasLarge_;
        }
    }
    return uiLarge_;
}

void App::applyChanges() {
    std::vector<Change> changes;
    changes.swap(pending_);
    for (Change& c : changes) {
        switch (c.kind) {
        case Change::Kind::Push: screens_.push_back(std::move(c.screen)); break;
        case Change::Kind::Replace:
            if (!screens_.empty()) screens_.pop_back();
            screens_.push_back(std::move(c.screen));
            break;
        case Change::Kind::Pop:
            if (!screens_.empty()) screens_.pop_back();
            break;
        case Change::Kind::Reset:
            screens_.clear();
            screens_.push_back(std::move(c.screen));
            break;
        }
    }
}

void App::update(int ms) {
    // The top screen and every screen whose upper neighbour lets it run.
    for (std::size_t i = screens_.size(); i-- > 0;) {
        screens_[i]->update(ms);
        if (!screens_[i]->updatesBelow()) break;
    }
}

void App::draw(render::IndexedSurface& target) {
    if (screens_.empty()) return;
    std::size_t first = screens_.size() - 1;
    while (first > 0 && screens_[first]->overlay()) --first;
    for (std::size_t i = first; i < screens_.size(); ++i) screens_[i]->draw(target);
}

const game::EncyclopediaData& App::encyclopedia() {
    if (!encyclopedia_) {
        encyclopedia_ = std::make_unique<game::EncyclopediaData>();
        try {
            if (!data_.global.empty()) *encyclopedia_ = game::EncyclopediaData::load(data::SqliteDb(data_.global));
        } catch (const std::exception&) {
            *encyclopedia_ = {};
        }
    }
    return *encyclopedia_;
}

const render::SmkFrames* App::encyclopediaPictures() {
    if (!picturesLoaded_) {
        picturesLoaded_ = true;
        try {
            encyclopediaPictures_ =
                std::make_unique<render::SmkFrames>(render::SmkFrames::load(data_.file("ENCYCL/ENCYCL.SMK")));
        } catch (const std::exception&) {
            encyclopediaPictures_.reset();
        }
    }
    return encyclopediaPictures_ && !encyclopediaPictures_->empty() ? encyclopediaPictures_.get() : nullptr;
}

const formats::DbiSprite* App::exeBitmap(int id, bool keyed) {
    if (const auto it = exeBitmaps_.find({id, keyed}); it != exeBitmaps_.end())
        return it->second.width > 0 ? &it->second : nullptr;
    if (!exeLoaded_) {
        exeLoaded_ = true;
        try {
            exeBitmapData_ = formats::loadResources(data_.file("EXE/LIBDAY.EXE"), formats::kResourceBitmap);
        } catch (const std::exception&) {
            exeBitmapData_.clear();
        }
    }
    formats::DbiSprite& out = exeBitmaps_[{id, keyed}];
    const auto raw = exeBitmapData_.find(id);
    if (raw == exeBitmapData_.end()) return nullptr;
    try {
        const formats::DibImage img = formats::decodeDib(raw->second);
        out.width = img.width;
        out.height = img.height;
        out.pixels.resize(img.pixels.size());
        out.opaque.assign(img.pixels.size(), 1);
        std::map<int, std::uint8_t> cache;
        for (std::size_t i = 0; i < img.pixels.size(); ++i) {
            const formats::Rgb c = img.pixels[i];
            const int key = (c.r << 16) | (c.g << 8) | c.b;
            const auto hit = cache.find(key);
            out.pixels[i] = hit != cache.end() ? hit->second : (cache[key] = nearestColor(data_.palette, c.r, c.g, c.b));
            // OpenDay: the color key of the marker bitmaps is the top-left pixel's color.
            if (keyed && c == img.pixels[0]) out.opaque[i] = 0;
        }
    } catch (const std::exception&) {
        out = {};
        return nullptr;
    }
    return &out;
}

void App::sound(const std::string& file) {
    audio_.soundEnabled = settings_.sound;
    audio_.playWav(data_.file("SOUNDS/" + file));
}

void App::startMusic() {
    if (!settings_.music) return;
    // A random logical track 2 ... min(N, 12); logical track n is TrackNN with
    // NN = n + 1 (interface.md, "Sounds and music").
    std::vector<fs::path> tracks;
    for (int n = 2; n <= 12; ++n) {
        char name[32];
        std::snprintf(name, sizeof name, "Track%02d.ogg", n + 1);
        const fs::path p = data_.file(std::string("EXE/MUSIC/") + name);
        if (fs::exists(p)) tracks.push_back(p);
    }
    if (tracks.empty()) return;
    std::random_device rd;
    audio_.playMusic(tracks[rd() % tracks.size()], true);
}

} // namespace openday::app
