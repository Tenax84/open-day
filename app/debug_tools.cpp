#include "app/debug_tools.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <typeinfo>

#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#endif

#include "app/battle_control.hpp"
#include "app/screens.hpp"
#include "battle/combat.hpp"
#include "battle/movement.hpp"
#include "battle/objectives.hpp"
#include "formats/png_writer.hpp"

namespace openday::app {

namespace {

using formats::JsonValue;
using formats::JsonWriter;
using Clock = std::chrono::steady_clock;

// --- Arguments and results ---------------------------------------------------

int argInt(const JsonValue& a, const char* key, int fallback) {
    const JsonValue& v = a[key];
    return v.type() == JsonValue::Type::Number ? static_cast<int>(std::lround(v.number())) : fallback;
}

double argNumber(const JsonValue& a, const char* key, double fallback) { return a[key].number(fallback); }

bool argBool(const JsonValue& a, const char* key, bool fallback) { return a[key].boolean(fallback); }

std::string argString(const JsonValue& a, const char* key) { return a[key].string(); }

std::vector<int> argInts(const JsonValue& a, const char* key) {
    std::vector<int> out;
    if (a[key].type() == JsonValue::Type::Number) out.push_back(argInt(a, key, 0));
    for (const JsonValue& v : a[key].items())
        if (v.type() == JsonValue::Type::Number) out.push_back(static_cast<int>(std::lround(v.number())));
    return out;
}

struct ArgError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

int requireInt(const JsonValue& a, const char* key) {
    if (a[key].type() != JsonValue::Type::Number) throw ArgError(std::string("missing number argument: ") + key);
    return argInt(a, key, 0);
}

std::string textItem(std::string_view text) {
    return "{\"type\":\"text\",\"text\":" + formats::jsonQuote(text) + "}";
}

ToolResult textResult(std::string_view text, bool error = false) { return {"[" + textItem(text) + "]", error}; }

std::string base64(const std::vector<std::uint8_t>& data) {
    static const char* digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < data.size(); i += 3) {
        const std::uint32_t n = static_cast<std::uint32_t>(data[i]) << 16 |
                                (i + 1 < data.size() ? static_cast<std::uint32_t>(data[i + 1]) << 8 : 0) |
                                (i + 2 < data.size() ? static_cast<std::uint32_t>(data[i + 2]) : 0);
        out += digits[(n >> 18) & 63];
        out += digits[(n >> 12) & 63];
        out += i + 1 < data.size() ? digits[(n >> 6) & 63] : '=';
        out += i + 2 < data.size() ? digits[n & 63] : '=';
    }
    return out;
}

// The class name of a screen, without namespaces ("BattleScreen").
std::string screenName(const Screen& s) {
    std::string n = typeid(s).name();
#if defined(__GNUC__) || defined(__clang__)
    int status = 0;
    if (char* d = abi::__cxa_demangle(n.c_str(), nullptr, nullptr, &status)) {
        n = d;
        std::free(d);
    }
#endif
    if (const auto p = n.rfind("::"); p != std::string::npos) n = n.substr(p + 2);
    for (const char* prefix : {"class ", "struct "})
        if (n.rfind(prefix, 0) == 0) n = n.substr(std::string(prefix).size());
    return n;
}

int keyCode(std::string k) {
    std::string l = k;
    std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (l == "enter" || l == "return") return ui::kKeyEnter;
    if (l == "esc" || l == "escape") return ui::kKeyEscape;
    if (l == "backspace") return ui::kKeyBackspace;
    if (l == "tab") return ui::kKeyTab;
    if (l == "space") return ' ';
    if (l == "left") return ui::kKeyLeft;
    if (l == "right") return ui::kKeyRight;
    if (l == "up") return ui::kKeyUp;
    if (l == "down") return ui::kKeyDown;
    if (l == "pageup") return ui::kKeyPageUp;
    if (l == "pagedown") return ui::kKeyPageDown;
    if (l == "home") return ui::kKeyHome;
    if (l == "end") return ui::kKeyEnd;
    if (l.size() == 2 && l[0] == 'f' && l[1] >= '1' && l[1] <= '6') return ui::kKeyF1 + (l[1] - '1');
    if (k.size() == 1) return static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(k[0])));
    return 0;
}

const char* shotErrorName(game::ShotError e) {
    switch (e) {
    case game::ShotError::None: return "ok";
    case game::ShotError::NoWeapon: return "no weapon";
    case game::ShotError::NotEnoughAp: return "not enough AP";
    case game::ShotError::OutOfRange: return "out of range";
    case game::ShotError::TooClose: return "too close";
    case game::ShotError::NoLineOfFire: return "no line of fire";
    default: return "invalid target";
    }
}

const char* categoryName(int c) { return c == 0 ? "grav" : c == 2 ? "water" : "ground"; }

void point(JsonWriter& w, const char* key, game::Point p) {
    w.key(key).beginArray().value(p.x).value(p.y).endArray();
}

// --- Battle state as JSON ----------------------------------------------------

void unitJson(JsonWriter& w, const BattleControl& b, int i, bool detail) {
    const game::BattleState& s = b.session().state();
    const game::BattleUnit& u = s.unit(i);
    w.beginObject().field("index", i).field("id", u.id).field("name", u.type->name).field("typeId", u.type->typeId);
    w.field("techLevel", u.type->techLevel).field("owner", u.owner);
    w.field("x", u.pos.x).field("y", u.pos.y).field("facing", u.facing);
    w.field("ap", u.availableAp()).field("apMax", u.type->actionPoints);
    if (u.negAp) w.field("negAp", u.negAp);
    w.field("armor", u.armor).field("armorMax", u.type->armor).field("category", categoryName(u.type->category));
    if (!u.alive) w.field("alive", false);
    if (u.type->hasWeapon) w.field("range", u.type->range).field("shotAp", u.type->shotAp);
    if (u.order.order != 0) w.field("order", u.order.order);
    if (u.alive) {
        const game::Point sp = b.screenPos(u.pos);
        const ui::Rect v = b.mapView();
        if (v.contains(sp.x, sp.y)) point(w, "screen", sp);
    }
    if (detail) {
        const game::UnitType& t = *u.type;
        w.field("movement", t.movement).field("size", std::to_string(t.sizeX) + "x" + std::to_string(t.sizeY));
        if (t.isConvoy) w.field("convoy", true);
        if (t.mineDetectionRange) w.field("mineDetection", t.mineDetectionRange);
        if (t.jammer) w.field("jammer", t.jammer);
        if (t.hasWeapon) {
            w.key("weapon").beginObject().field("range", t.range).field("minRange", t.minRange).field("shotAp", t.shotAp);
            w.field("indirect", t.indirect).field("apDamage", t.apDamage);
            w.field("damage", std::to_string(t.damageBase) + "+" + std::to_string(t.damageRandom));
            w.field("area", t.damageArea).field("canOverwatch", t.canOverwatch);
            w.key("hit").beginArray().value(t.hit[0]).value(t.hit[1]).value(t.hit[2]).endArray().endObject();
        }
        if (u.order.order != 0) {
            w.key("scriptedOrder").beginObject().field("order", u.order.order).field("turn", u.order.turn);
            w.field("probability", u.order.probability).endObject();
        }
    }
    w.endObject();
}

void buildingJson(JsonWriter& w, const game::BattleState& s, int i) {
    const game::BattleBuilding& b = s.building(i);
    w.beginObject().field("index", i).field("id", b.id).field("name", b.type->name).field("typeId", b.type->id);
    w.field("owner", b.owner).field("x", b.pos.x).field("y", b.pos.y);
    w.field("size", std::to_string(b.type->sizeX) + "x" + std::to_string(b.type->sizeY));
    w.field("armor", b.armor).field("armorMax", b.type->armor);
    if (!b.type->destructible) w.field("indestructible", true);
    if (b.type->techReward) w.field("techReward", b.type->techReward);
    if (!b.alive) w.field("alive", false);
    w.endObject();
}

std::string battleSummary(const BattleControl& b) {
    const game::BattleState& s = b.session().state();
    JsonWriter w;
    w.beginObject().field("mode", b.modeName()).field("dialog", b.dialogName()).field("turn", s.turn);
    w.field("currentPlayer", s.currentPlayer).field("over", s.over);
    if (s.over) w.field("success", s.success);
    w.field("animationsBusy", b.session().animations().busy()).endObject();
    return w.str();
}

std::string battleState(const BattleControl& b, const JsonValue& a) {
    const game::BattleState& s = b.session().state();
    const int local = b.localPlayer();
    std::vector<std::string> include = {"units", "buildings"};
    if (!a["include"].items().empty()) {
        include.clear();
        for (const JsonValue& v : a["include"].items()) include.push_back(v.string());
    }
    auto wants = [&](const char* k) { return std::find(include.begin(), include.end(), k) != include.end(); };
    const bool dead = argBool(a, "dead", false);
    const int owner = argInt(a, "owner", -1);

    JsonWriter w;
    w.beginObject().field("scenario", b.session().name()).field("mode", b.modeName()).field("dialog", b.dialogName());
    w.field("localPlayer", local).field("mapSize", s.size()).field("turn", s.turn).field("turnLimit", s.turnLimit);
    w.field("currentPlayer", s.currentPlayer).field("displayedPlayer", b.session().displayedPlayer());
    w.field("deploying", s.deploying).field("over", s.over);
    if (s.over) w.field("success", s.success);
    w.field("animationsBusy", b.session().animations().busy());
    w.key("players").beginArray();
    for (const int p : s.players) w.value(p);
    w.endArray();
    w.field("budget", s.deployBudget[static_cast<std::size_t>(local)]).field("income", s.income[static_cast<std::size_t>(local)]);
    w.key("objective").beginObject().field("kind", s.objective.kind).field("name", game::objectiveName(s.objective.kind));
    w.field("turns", s.objective.turns).field("progress", s.objectiveProgress);
    w.key("ids").beginArray();
    for (const int id : s.objective.ids) w.value(id);
    w.endArray().key("zone").beginArray();
    for (const game::Point p : s.objective.zone) w.beginArray().value(p.x).value(p.y).endArray();
    w.endArray().endObject();
    w.key("selection").beginArray();
    for (const int u : b.selected()) w.value(u);
    w.endArray();
    if (const std::string m = b.messageText(); !m.empty()) w.field("message", m);
    // The tiles at the corners of the map view (what part of the map is shown).
    const ui::Rect v = b.mapView();
    w.key("view").beginObject();
    point(w, "topLeft", b.tileAt(v.x, v.y));
    point(w, "topRight", b.tileAt(v.x + v.w - 1, v.y));
    point(w, "bottomLeft", b.tileAt(v.x, v.y + v.h - 1));
    point(w, "bottomRight", b.tileAt(v.x + v.w - 1, v.y + v.h - 1));
    w.endObject();

    if (wants("units")) {
        w.key("units").beginArray();
        for (int i = 0; i < static_cast<int>(s.units().size()); ++i) {
            const game::BattleUnit& u = s.unit(i);
            if ((u.alive || dead) && (owner < 0 || u.owner == owner)) unitJson(w, b, i, false);
        }
        w.endArray();
    }
    if (wants("buildings")) {
        w.key("buildings").beginArray();
        for (int i = 0; i < static_cast<int>(s.buildings().size()); ++i) {
            const game::BattleBuilding& bb = s.building(i);
            if ((bb.alive || dead) && (owner < 0 || bb.owner == owner)) buildingJson(w, s, i);
        }
        w.endArray();
    }
    if (wants("walls")) {
        w.key("walls").beginArray();
        for (int i = 0; i < static_cast<int>(s.walls().size()); ++i) {
            const game::BattleWall& x = s.wall(i);
            if (!x.alive && !dead) continue;
            w.beginObject().field("index", i).field("type", x.type).field("owner", x.owner).field("x", x.pos.x).field("y", x.pos.y);
            w.field("armor", x.armor).endObject();
        }
        w.endArray();
    }
    if (wants("mines")) {
        w.key("mines").beginArray();
        for (int i = 0; i < static_cast<int>(s.mines().size()); ++i) {
            const game::BattleMine& m = s.mine(i);
            if (!m.alive && !dead) continue;
            w.beginObject().field("index", i).field("type", m.type).field("owner", m.owner).field("x", m.pos.x).field("y", m.pos.y);
            w.field("armed", m.armed).field("hiddenFromYou", m.hidden[static_cast<std::size_t>(local)]).endObject();
        }
        w.endArray();
    }
    if (wants("roads")) {
        w.key("roads").beginArray();
        for (const game::BattleRoad& r : s.roads())
            if (r.alive || dead) w.beginArray().value(r.pos.x).value(r.pos.y).value(r.type).endArray();
        w.endArray();
    }
    if (wants("ruins")) {
        w.key("ruins").beginArray();
        for (const game::BattleRuin& r : s.ruins()) w.beginObject().field("name", r.name).field("x", r.pos.x).field("y", r.pos.y).endObject();
        w.endArray();
    }
    if (wants("deploy")) {
        w.key("deployZone").beginArray();
        for (const game::Point p : s.deployZone[static_cast<std::size_t>(local)]) w.beginArray().value(p.x).value(p.y).endArray();
        w.endArray();
        w.key("reinforcements").beginArray();
        for (const game::Reinforcement& r : s.reinforcements)
            w.beginObject().field("type", r.type).field("x", r.pos.x).field("y", r.pos.y).field("turn", r.turn).endObject();
        w.endArray();
        w.key("automated").beginArray();
        for (const int id : s.usableAutomated(local)) {
            const auto it = s.rules().automated.find(id);
            w.beginObject().field("typeId", id);
            if (it != s.rules().automated.end()) w.field("name", it->second.name).field("cost", it->second.cost);
            w.endObject();
        }
        w.endArray();
    }
    w.endObject();
    return w.str();
}

std::string unitDetail(const BattleControl& b, int i) {
    const game::BattleState& s = b.session().state();
    JsonWriter w;
    w.beginObject().key("unit");
    unitJson(w, b, i, true);
    // Possible targets: the enemy units and buildings, nearest first.
    struct Target {
        bool building;
        int index;
        game::Point pos;
        int distance;
    };
    std::vector<Target> targets;
    const game::BattleUnit& u = s.unit(i);
    for (int k = 0; k < static_cast<int>(s.units().size()); ++k) {
        const game::BattleUnit& o = s.unit(k);
        if (o.alive && game::unitSide(u, o, true) == game::Side::Enemy)
            targets.push_back({false, k, o.pos, game::chebyshev(u.pos, o.pos)});
    }
    for (int k = 0; k < static_cast<int>(s.buildings().size()); ++k) {
        const game::BattleBuilding& o = s.building(k);
        if (o.alive && o.type->destructible && game::buildingSide(u.owner, o, true) == game::Side::Enemy)
            targets.push_back({true, k, o.pos, game::chebyshev(u.pos, o.pos)});
    }
    std::sort(targets.begin(), targets.end(), [](const Target& a, const Target& c) { return a.distance < c.distance; });
    if (targets.size() > 25) targets.resize(25);
    w.key("targets").beginArray();
    for (const Target& t : targets) {
        w.beginObject().field("kind", t.building ? "building" : "unit").field("index", t.index);
        w.field("name", t.building ? s.building(t.index).type->name : s.unit(t.index).type->name);
        w.field("x", t.pos.x).field("y", t.pos.y).field("distance", t.distance);
        const game::ShotCheck c = game::checkShot(s, i, t.pos, true);
        w.field("shot", shotErrorName(c.error));
        if (c.ok()) w.field("hitChance", std::min(100, game::hitChance(s, i, c.aim)));
        w.endObject();
    }
    w.endArray().endObject();
    return w.str();
}

std::string pathInfo(const BattleControl& b, int unit, game::Point target, bool overwatch) {
    const game::BattleState& s = b.session().state();
    const game::PlannedMove m = game::planMove(s, unit, target, overwatch);
    JsonWriter w;
    w.beginObject().field("unit", unit).field("ap", s.unit(unit).availableAp()).field("reachable", !m.path.empty());
    int total = 0;
    for (const game::PathStep& st : m.path) total += st.cost;
    w.field("steps", static_cast<int>(m.path.size())).field("cost", total).field("affordableSteps", m.affordable);
    w.field("apThisTurn", game::moveCost(m.path, m.affordable, s.unit(unit).availableAp()));
    if (m.affordable > 0) point(w, "stopsAt", m.path[static_cast<std::size_t>(m.affordable - 1)].pos);
    w.key("path").beginArray();
    for (const game::PathStep& st : m.path) w.beginArray().value(st.pos.x).value(st.pos.y).value(st.cost).endArray();
    w.endArray();
    if (m.path.empty()) {
        const game::TileEval e = game::evaluateTile(s, s.unit(unit), target);
        w.field("blockReason", static_cast<int>(e.reason));
    }
    w.endObject();
    return w.str();
}

// A text picture of the map (battle_map).
std::string asciiMap(const BattleControl& b, int x0, int y0, int w, int h) {
    const game::BattleState& s = b.session().state();
    const int local = b.localPlayer();
    std::string out;
    out += "x " + std::to_string(x0) + ".." + std::to_string(x0 + w - 1) + " left to right, y " + std::to_string(y0) + ".." +
           std::to_string(y0 + h - 1) + " top to bottom (on screen +x runs down-right, +y down-left)\n";
    out += "     ";
    for (int x = x0; x < x0 + w; ++x) out += static_cast<char>('0' + (x / 10) % 10);
    out += "\n     ";
    for (int x = x0; x < x0 + w; ++x) out += static_cast<char>('0' + x % 10);
    out += "\n";
    for (int y = y0; y < y0 + h; ++y) {
        char label[8];
        std::snprintf(label, sizeof label, "%4d ", y);
        out += label;
        for (int x = x0; x < x0 + w; ++x) {
            const game::Point p{x, y};
            char c = '?';
            switch (s.classAt(p)) {
            case game::TerrainClass::DeepWater: c = '~'; break;
            case game::TerrainClass::ShallowWater: c = '-'; break;
            case game::TerrainClass::Riverside: c = ','; break;
            case game::TerrainClass::Plain: c = '.'; break;
            case game::TerrainClass::Mountains: c = '^'; break;
            case game::TerrainClass::Forest: c = 'T'; break;
            case game::TerrainClass::Crack: c = '%'; break;
            default: break;
            }
            if (s.deploying && s.inDeployZone(local, p)) c = 'o';
            if (s.roadAt(p) >= 0) c = '=';
            if (s.hasRuin(p)) c = 'r';
            if (const int m = s.mineAt(p); m >= 0) c = s.mine(m).hidden[static_cast<std::size_t>(local)] ? '+' : '*';
            if (s.wallAt(p) >= 0) c = '#';
            if (const int bi = s.buildingAt(p); bi >= 0) {
                const game::Side side = game::buildingSide(local, s.building(bi), true);
                c = side == game::Side::Same ? 'H' : side == game::Side::Ally ? 'h' : 'X';
            }
            if (const int u = s.unitAt(p); u >= 0) {
                const game::BattleUnit& unit = s.unit(u);
                if (unit.owner == local) c = unit.availableAp() > 0 ? 'U' : 'u';
                else c = unit.owner == 0 ? 'E' : 'A';
            }
            out += c;
        }
        out += '\n';
    }
    out += "legend: U/u own unit (with / without AP), E enemy unit, A allied unit, H own building, h allied, X enemy "
           "building, # wall, * mine, + mine hidden from you, r ruin, = road, o your deployment zone, ~ deep water, - shallow "
           "water, , riverside, . plain, ^ mountains, T forest, % crack\n";
    return out;
}

// --- Tool descriptions ------------------------------------------------------

const char* const kWaitShot =
    R"J("wait_ms":{"type":"integer","description":"Game milliseconds to let pass before answering; default 150"},)J"
    R"J("screenshot":{"type":"boolean","description":"Attach a screenshot taken after the wait"})J";

std::string schema(const std::string& properties, const std::string& required = "") {
    std::string s = R"J({"type":"object","properties":{)J" + properties + "}";
    if (!required.empty()) s += R"J(,"required":[)J" + required + "]";
    return s + "}";
}

const char* const kOrderWait =
    R"J("wait":{"type":"boolean","description":"Wait until the animations have played and it is your turn again, or the battle ended; default true"},)J"
    R"J("timeout_ms":{"type":"integer","description":"Real-time limit of the wait; default 120000"},)J"
    R"J("screenshot":{"type":"boolean","description":"Attach a screenshot when done"})J";

} // namespace

std::vector<ToolInfo> DebugTools::toolList() {
    const std::string xy = R"J("x":{"type":"integer"},"y":{"type":"integer"},)J";
    const std::string units = R"J("units":{"type":"array","items":{"type":"integer"},"description":"Unit indices (battle_state)"},)J";
    return {
        {"get_screen", "The stack of screens (bottom first), the top screen's logical size, the game speed and, in a battle, its mode and turn.",
         schema("")},
        {"screenshot", "A PNG of the current frame at the logical resolution of the top screen (640x480 or 1024x768). Its pixel coordinates are the ones click and mouse_move take.",
         schema("")},
        {"click", "Clicks at logical screen coordinates (as in the screenshot): mouse move, button down, button up.",
         schema(xy + R"J("button":{"type":"string","enum":["left","right","middle"]},"hold":{"type":"boolean","description":"Press without releasing (the right button shows the info card while held)"},)J" + kWaitShot,
                R"J("x","y")J")},
        {"mouse_move", "Moves the mouse to logical screen coordinates (hover, edge scrolling).", schema(xy + kWaitShot, R"J("x","y")J")},
        {"key", "Presses a key: a letter or digit, or enter, esc, backspace, tab, space, up, down, left, right, pageup, pagedown, home, end, f1-f6.",
         schema(R"J("key":{"type":"string"},"times":{"type":"integer"},"shift":{"type":"boolean"},"ctrl":{"type":"boolean"},)J" + std::string(kWaitShot), R"J("key")J")},
        {"type_text", "Types text, one key press per character (names in the save dialog and the like).",
         schema(R"J("text":{"type":"string"},)J" + std::string(kWaitShot), R"J("text")J")},
        {"wait", "Lets the game run for some game milliseconds.",
         schema(R"J("ms":{"type":"integer"},"screenshot":{"type":"boolean"})J", R"J("ms")J")},
        {"wait_idle", "In a battle: waits until the animations have played and it is your turn (or deployment) again, or the battle ended.",
         schema(R"J("timeout_ms":{"type":"integer","description":"Real-time limit; default 120000"},"screenshot":{"type":"boolean"})J")},
        {"set_speed", "Sets the game speed factor (0.1-20; 1 = normal). Animations and waits in game time run that much faster.",
         schema(R"J("speed":{"type":"number"})J", R"J("speed")J")},
        {"list_scenarios", "The skirmish scenarios (index, title, province, map size, deployment budget).", schema("")},
        {"start_skirmish", "Starts a skirmish scenario at once, from any screen: by index (list_scenarios) or by title or file name.",
         schema(R"J("scenario":{"type":["integer","string"]},"briefing":{"type":"boolean","description":"Show the mission briefing first; default false"},"screenshot":{"type":"boolean"})J",
                R"J("scenario")J")},
        {"main_menu", "Leaves whatever runs and shows the main menu.", schema(R"J("screenshot":{"type":"boolean"})J")},
        {"battle_state", "The running battle: mode (deploy/tactic/wait/automated), turn, players, objective, budget, the map view and the objects. Units carry index (used by the battle tools), owner (0 = computer, 1 = you), position, AP, armor and, when visible, their screen position.",
         schema(R"J("include":{"type":"array","items":{"type":"string","enum":["units","buildings","walls","mines","roads","ruins","deploy"]},"description":"Sections (default units and buildings); deploy = zone, pending reinforcements and automated units"},)J"
                R"J("owner":{"type":"integer","description":"Only objects of this player slot"},"dead":{"type":"boolean","description":"Include destroyed objects"})J")},
        {"battle_unit", "One unit in detail (weapon, scripted order) with the enemy units and buildings nearest to it and whether it can fire at them now (with the hit chance).",
         schema(R"J("unit":{"type":"integer"})J", R"J("unit")J")},
        {"battle_map", "A text picture of the battle map (terrain, roads, walls, mines, buildings, units). Default: the whole map.",
         schema(R"J("x":{"type":"integer"},"y":{"type":"integer"},"w":{"type":"integer"},"h":{"type":"integer"})J")},
        {"battle_path", "The path a unit would take to a tile: steps, AP cost, how far it gets this turn.",
         schema(R"J("unit":{"type":"integer"},)J" + xy.substr(0, xy.size() - 1), R"J("unit","x","y")J")},
        {"battle_center", "Centres the map view on a tile.", schema(xy + R"J("screenshot":{"type":"boolean"})J", R"J("x","y")J")},
        {"battle_deploy_types", "The unit types for sale now (deployment, or reinforcements in your turn) with cost and budget.", schema("")},
        {"battle_deploy", "Buys a unit and places it on a tile of your deployment zone (in your turn: a reinforcement).",
         schema(R"J("type_id":{"type":"integer"},)J" + xy + R"J("screenshot":{"type":"boolean"})J", R"J("type_id","x","y")J")},
        {"battle_undeploy", "Takes back a unit placed during this deployment (refund).", schema(R"J("unit":{"type":"integer"})J", R"J("unit")J")},
        {"battle_finish_deployment", "Ends the deployment (CONTINUE) and starts the battle, or ends buying reinforcements.",
         schema(kOrderWait)},
        {"battle_move", "Moves your units toward a tile, as the two-click move of the interface (group rules, AP limit, Overwatch reserve). Answers with the events it caused.",
         schema(units + xy + kOrderWait, R"J("units","x","y")J")},
        {"battle_shoot", "Your units fire at a tile (walking to a firing position first where needed and possible). Answers with the events it caused.",
         schema(units + xy + kOrderWait, R"J("units","x","y")J")},
        {"battle_end_turn", "Ends your turn; with wait (default) answers after the other side's turn has played, with all the events.",
         schema(kOrderWait)},
        {"battle_events", "The battle's event log (described events), from an index on.",
         schema(R"J("since":{"type":"integer"},"limit":{"type":"integer"})J")},
        {"battle_force_result", "Debugging: ends the battle as won or lost (the cheat keys' request).",
         schema(R"J("win":{"type":"boolean"},"wait":{"type":"boolean"},"timeout_ms":{"type":"integer"},"screenshot":{"type":"boolean"})J", R"J("win")J")},
    };
}

std::string DebugTools::instructions() {
    return "OpenDay debug server: watch and drive the running game. Coordinates for click and mouse_move are logical "
           "screen pixels, the same as in screenshot. In a battle, prefer the battle_* tools: battle_state lists the units "
           "by index (owner 1 = you, 0 = the computer); battle_deploy / battle_finish_deployment for the deployment, then "
           "battle_move, battle_shoot and battle_end_turn. At the start of a deployment the tactical map is shown over "
           "the battle: close it with key esc first. Orders wait until their animations have played; set_speed makes "
           "that faster.";
}

DebugTools::~DebugTools() {
    if (pending_) pending_->call->result.set_value(textResult("the game is closing", true));
    for (const Queued& q : queue_) q.call->result.set_value(textResult("the game is closing", true));
}

void DebugTools::frame(McpServer& server, const render::IndexedSurface& picture, const formats::Palette& palette, int elapsedMs) {
    picture_ = &picture;
    palette_ = &palette;
    for (auto& c : server.takeCalls()) queue_.push_back({std::move(c), Clock::now()});
    auto finish = [&](const std::shared_ptr<ToolCall>& call, const std::function<ToolResult()>& f) {
        ToolResult r;
        try {
            r = f();
        } catch (const std::exception& e) {
            r = textResult(e.what(), true);
        }
        call->result.set_value(std::move(r));
    };
    if (pending_) {
        pending_->waitMs -= elapsedMs;
        const bool late = Clock::now() > pending_->deadline;
        if (late || (pending_->waitMs <= 0 && (!pending_->until || pending_->until()))) {
            const std::unique_ptr<Pending> p = std::move(pending_);
            finish(p->call, [&] { return p->finish(late); });
        }
    }
    while (!pending_ && !queue_.empty()) {
        // A battle order waits until the animations of the one before have
        // played, as the interface takes no clicks meanwhile (up to a minute).
        const std::string& next = queue_.front().call->name;
        const bool order = next == "battle_deploy" || next == "battle_undeploy" || next == "battle_finish_deployment" ||
                           next == "battle_move" || next == "battle_shoot" || next == "battle_end_turn" ||
                           next == "battle_force_result";
        if (order && battle() && battle()->session().animations().busy() &&
            Clock::now() - queue_.front().since < std::chrono::minutes(1))
            break;
        const std::shared_ptr<ToolCall> call = queue_.front().call;
        queue_.pop_front();
        std::optional<ToolResult> r;
        try {
            r = start(call);
        } catch (const std::exception& e) {
            pending_.reset();
            r = textResult(e.what(), true);
        }
        if (r) call->result.set_value(std::move(*r));
    }
}

void DebugTools::defer(const std::shared_ptr<ToolCall>& call, int waitMs, int timeoutMs, std::function<bool()> until,
                       std::function<ToolResult(bool)> finish) {
    pending_ = std::make_unique<Pending>();
    pending_->call = call;
    pending_->waitMs = waitMs;
    pending_->until = std::move(until);
    pending_->deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    pending_->finish = std::move(finish);
}

std::optional<ToolResult> DebugTools::reply(const std::shared_ptr<ToolCall>& call, std::function<std::string()> text, int waitMs) {
    const bool shot = argBool(call->args, "screenshot", false);
    if (waitMs <= 0 && !shot) return textResult(text());
    // The answer comes after the next frame(s), so that the screenshot shows the result.
    const int timeout = static_cast<int>(waitMs / std::max(speed_, 0.1)) + 30000;
    defer(call, waitMs, timeout, {}, [this, shot, text](bool) {
        std::string content = "[" + textItem(text());
        if (shot) content += "," + screenshotContent();
        return ToolResult{content + "]", false};
    });
    return std::nullopt;
}

void DebugTools::send(const ui::InputEvent& e) {
    if (app_.top()) app_.top()->handle(e);
    app_.applyChanges();
}

std::string DebugTools::screenshotContent() const {
    if (!picture_ || !palette_) return textItem("no frame yet");
    std::vector<std::uint8_t> pixels;
    pixels.reserve(static_cast<std::size_t>(picture_->width()) * static_cast<std::size_t>(picture_->height()));
    for (int y = 0; y < picture_->height(); ++y) pixels.insert(pixels.end(), picture_->row(y), picture_->row(y) + picture_->width());
    std::vector<std::uint8_t> rgb;
    rgb.reserve(768);
    for (const formats::Rgb& c : *palette_) rgb.insert(rgb.end(), {c.r, c.g, c.b});
    const std::vector<std::uint8_t> png = formats::encodePngIndexed(picture_->width(), picture_->height(), pixels, rgb);
    return R"J({"type":"image","mimeType":"image/png","data":")J" + base64(png) + "\"}";
}

BattleControl* DebugTools::battle() const {
    const auto& screens = app_.screens();
    for (auto it = screens.rbegin(); it != screens.rend(); ++it)
        if (BattleControl* b = (*it)->battleControl()) return b;
    return nullptr;
}

bool DebugTools::battleSettled() const {
    BattleControl* b = battle();
    if (!b) return true;
    const game::BattleSession& s = b->session();
    if (s.animations().busy()) return false;
    return s.displayedOver() || b->modeName() != "wait";
}

std::string DebugTools::screenInfo() const {
    JsonWriter w;
    w.beginObject().key("screens").beginArray();
    for (const auto& s : app_.screens()) w.value(screenName(*s));
    w.endArray();
    if (Screen* top = app_.screens().empty() ? nullptr : app_.screens().back().get()) {
        w.field("top", screenName(*top)).field("width", top->width()).field("height", top->height());
    }
    w.field("speed", speed_);
    if (BattleControl* b = battle()) w.key("battle").raw(battleSummary(*b));
    w.endObject();
    return w.str();
}

std::optional<ToolResult> DebugTools::start(const std::shared_ptr<ToolCall>& call) {
    const std::string& name = call->name;
    const JsonValue& a = call->args;
    const int waitMs = std::clamp(argInt(a, "wait_ms", 150), 0, 600000);

    if (name == "get_screen") return textResult(screenInfo());
    if (name == "screenshot") {
        const Screen* top = app_.top();
        const std::string text = top ? screenName(*top) + " " + std::to_string(picture_ ? picture_->width() : 0) + "x" +
                                           std::to_string(picture_ ? picture_->height() : 0)
                                     : "no screen";
        return ToolResult{"[" + textItem(text) + "," + screenshotContent() + "]", false};
    }
    if (name == "click" || name == "mouse_move") {
        ui::InputEvent e;
        e.x = requireInt(a, "x");
        e.y = requireInt(a, "y");
        e.type = ui::InputEvent::Type::MouseMove;
        send(e);
        if (name == "click") {
            const std::string button = argString(a, "button");
            e.button = button == "right" ? 3 : button == "middle" ? 2 : 1;
            e.type = ui::InputEvent::Type::MouseDown;
            send(e);
            if (!argBool(a, "hold", false)) {
                e.type = ui::InputEvent::Type::MouseUp;
                send(e);
            }
        }
        return reply(call, [this] { return screenInfo(); }, waitMs);
    }
    if (name == "key") {
        ui::InputEvent e;
        e.type = ui::InputEvent::Type::KeyDown;
        e.key = keyCode(argString(a, "key"));
        if (e.key == 0) throw ArgError("unknown key: " + argString(a, "key"));
        e.shift = argBool(a, "shift", false);
        e.ctrl = argBool(a, "ctrl", false);
        for (int i = std::clamp(argInt(a, "times", 1), 1, 100); i > 0; --i) send(e);
        return reply(call, [this] { return screenInfo(); }, waitMs);
    }
    if (name == "type_text") {
        for (const char ch : argString(a, "text")) {
            ui::InputEvent e;
            e.type = ui::InputEvent::Type::KeyDown;
            const auto c = static_cast<unsigned char>(ch);
            e.shift = std::isupper(c) != 0;
            e.key = std::tolower(c);
            send(e);
        }
        return reply(call, [this] { return screenInfo(); }, waitMs);
    }
    if (name == "wait") {
        const int ms = std::clamp(requireInt(a, "ms"), 0, 600000);
        const bool shot = argBool(a, "screenshot", false);
        defer(call, std::max(ms, 1), static_cast<int>(ms / std::max(speed_, 0.1)) + 30000, {}, [this, shot](bool) {
            std::string content = "[" + textItem(screenInfo());
            if (shot) content += "," + screenshotContent();
            return ToolResult{content + "]", false};
        });
        return std::nullopt;
    }
    if (name == "wait_idle") {
        if (!battle()) return textResult("no battle is running; " + screenInfo());
        const bool shot = argBool(a, "screenshot", false);
        defer(call, 1, std::clamp(argInt(a, "timeout_ms", 120000), 100, 3600000), [this] { return battleSettled(); },
              [this, shot](bool late) {
                  std::string content = "[" + textItem((late ? "timed out; " : "") + screenInfo());
                  if (shot) content += "," + screenshotContent();
                  return ToolResult{content + "]", false};
              });
        return std::nullopt;
    }
    if (name == "set_speed") {
        speed_ = std::clamp(argNumber(a, "speed", 1), 0.1, 20.0);
        return textResult("speed " + std::to_string(speed_));
    }
    if (name == "list_scenarios") {
        JsonWriter w;
        w.beginArray();
        const auto& list = app_.data().skirmish;
        for (std::size_t i = 0; i < list.size(); ++i) {
            const ScenarioEntry& e = list[i];
            w.beginObject().field("index", static_cast<int>(i)).field("title", e.title).field("province", e.province);
            w.field("size", e.size).field("budget", e.budget).field("file", e.database.stem().string()).endObject();
        }
        w.endArray();
        return textResult(w.str());
    }
    if (name == "start_skirmish") {
        const auto& list = app_.data().skirmish;
        const JsonValue& s = a["scenario"];
        const ScenarioEntry* entry = nullptr;
        if (s.type() == JsonValue::Type::Number) {
            const int i = argInt(a, "scenario", -1);
            if (i >= 0 && i < static_cast<int>(list.size())) entry = &list[static_cast<std::size_t>(i)];
        } else {
            auto low = [](std::string t) {
                std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return t;
            };
            const std::string want = low(s.string());
            for (const ScenarioEntry& e : list)
                if (low(e.title) == want || low(e.database.stem().string()) == want) entry = &e;
        }
        if (!entry) throw ArgError("no such scenario (see list_scenarios)");
        app_.reset(argBool(a, "briefing", false) ? makeBriefing(app_, *entry) : makeBattle(app_, *entry));
        app_.applyChanges();
        return reply(call, [this] { return screenInfo(); }, 1);
    }
    if (name == "main_menu") {
        app_.reset(makeMainMenu(app_));
        app_.applyChanges();
        return reply(call, [this] { return screenInfo(); }, 1);
    }
    if (name.rfind("battle_", 0) == 0) {
        BattleControl* b = battle();
        if (!b) return textResult("no battle is running; " + screenInfo(), true);
        return battleTool(call, *b);
    }
    throw ArgError("unknown tool: " + name);
}

std::optional<ToolResult> DebugTools::battleTool(const std::shared_ptr<ToolCall>& call, BattleControl& b) {
    const std::string& name = call->name;
    const JsonValue& a = call->args;
    const game::BattleState& s = b.session().state();
    auto unitArg = [&](const char* key) {
        const int u = requireInt(a, key);
        if (u < 0 || u >= static_cast<int>(s.units().size())) throw ArgError("no unit with index " + std::to_string(u));
        return u;
    };

    if (name == "battle_state") return textResult(battleState(b, a));
    if (name == "battle_unit") return textResult(unitDetail(b, unitArg("unit")));
    if (name == "battle_map") {
        const int n = s.size();
        const int x = std::clamp(argInt(a, "x", 0), 0, n - 1), y = std::clamp(argInt(a, "y", 0), 0, n - 1);
        const int w = std::clamp(argInt(a, "w", n), 1, n - x), h = std::clamp(argInt(a, "h", n), 1, n - y);
        return textResult(asciiMap(b, x, y, w, h));
    }
    if (name == "battle_path") {
        const int u = unitArg("unit");
        const game::Point target{requireInt(a, "x"), requireInt(a, "y")};
        if (!s.inside(target)) throw ArgError("tile outside the map");
        return textResult(pathInfo(b, u, target, app_.settings().overwatch));
    }
    if (name == "battle_center") {
        const game::Point target{requireInt(a, "x"), requireInt(a, "y")};
        b.centerOn(target);
        return reply(call, [this, target] {
            const BattleControl* now = battle();
            if (!now) return std::string("the battle ended");
            JsonWriter w;
            w.beginObject();
            point(w, "tile", target);
            point(w, "screen", now->screenPos(target));
            w.endObject();
            return w.str();
        });
    }
    if (name == "battle_deploy_types") {
        const int local = b.localPlayer();
        JsonWriter w;
        w.beginObject().field("budget", s.deployBudget[static_cast<std::size_t>(local)]).key("types").beginArray();
        for (const int id : b.deployTypes()) {
            const game::UnitType* t = s.unitType(id, s.techLevel(local, id));
            if (!t) continue;
            w.beginObject().field("typeId", id).field("name", t->name).field("cost", t->cost).field("techLevel", t->techLevel);
            w.field("ap", t->actionPoints).field("armor", t->armor).field("category", categoryName(t->category));
            if (t->hasWeapon) w.field("range", t->range).field("shotAp", t->shotAp);
            w.endObject();
        }
        w.endArray().endObject();
        return textResult(w.str());
    }
    if (name == "battle_events") {
        const auto& log = b.eventLog();
        const int since = std::clamp(argInt(a, "since", 0), 0, static_cast<int>(log.size()));
        const int limit = std::clamp(argInt(a, "limit", 200), 1, 5000);
        JsonWriter w;
        w.beginObject().field("total", static_cast<int>(log.size())).field("since", since).key("events").beginArray();
        for (int i = since; i < static_cast<int>(log.size()) && i < since + limit; ++i) w.value(log[static_cast<std::size_t>(i)]);
        w.endArray().endObject();
        return textResult(w.str());
    }
    return battleOrder(call, b);
}

std::optional<ToolResult> DebugTools::battleOrder(const std::shared_ptr<ToolCall>& call, BattleControl& b) {
    const std::string& name = call->name;
    const JsonValue& a = call->args;
    const std::size_t before = b.eventLog().size();
    std::vector<int> units = argInts(a, "units");
    std::string refused;
    bool waits = argBool(a, "wait", true);
    if (name == "battle_deploy") {
        refused = b.deployUnit(requireInt(a, "type_id"), {requireInt(a, "x"), requireInt(a, "y")});
        if (refused.empty()) units = {static_cast<int>(b.session().state().units().size()) - 1};
        waits = false;
    } else if (name == "battle_undeploy") {
        refused = b.undeployUnit(requireInt(a, "unit"));
        waits = false;
    } else if (name == "battle_finish_deployment") {
        refused = b.endDeployment();
    } else if (name == "battle_move") {
        refused = b.moveUnits(units, {requireInt(a, "x"), requireInt(a, "y")});
    } else if (name == "battle_shoot") {
        refused = b.shootAt(units, {requireInt(a, "x"), requireInt(a, "y")});
    } else if (name == "battle_end_turn") {
        refused = b.finishTurn();
    } else if (name == "battle_force_result") {
        refused = b.forceResult(argBool(a, "win", true));
    } else {
        throw ArgError("unknown tool: " + name);
    }

    // The answer: the refusal or the events the order caused, the battle's
    // situation and the ordered units afterwards.
    auto answer = [this, refused, before, units](bool late) {
        JsonWriter w;
        w.beginObject().field("ok", refused.empty());
        if (!refused.empty()) w.field("error", refused);
        if (late) w.field("timedOut", true);
        BattleControl* now = battle();
        if (!now) {
            w.field("battle", "ended; " + screenInfo());
        } else {
            const auto& log = now->eventLog();
            constexpr std::size_t kMax = 300;
            w.key("events").beginArray();
            for (std::size_t i = before; i < log.size() && i < before + kMax; ++i) w.value(log[i]);
            w.endArray();
            if (log.size() > before + kMax)
                w.field("eventsTruncated", "more in battle_events since " + std::to_string(before + kMax));
            w.key("battle").raw(battleSummary(*now));
            const game::BattleState& s = now->session().state();
            w.key("units").beginArray();
            for (const int u : units)
                if (u >= 0 && u < static_cast<int>(s.units().size())) unitJson(w, *now, u, false);
            w.endArray();
        }
        w.endObject();
        return w.str();
    };
    const bool shot = argBool(a, "screenshot", false);
    if (!refused.empty() || (!waits && !shot)) return ToolResult{"[" + textItem(answer(false)) + "]", !refused.empty()};
    defer(call, 1, std::clamp(argInt(a, "timeout_ms", 120000), 100, 3600000),
          waits ? std::function<bool()>([this] { return battleSettled(); }) : std::function<bool()>(),
          [answer, shot, this](bool late) {
              std::string content = "[" + textItem(answer(late));
              if (shot) content += "," + screenshotContent();
              return ToolResult{content + "]", false};
          });
    return std::nullopt;
}

} // namespace openday::app
