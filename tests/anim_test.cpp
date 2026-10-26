// Unit animation rules (names, facings, placement, paths), the animation
// timeline and the action sequences, on synthetic data.

#include "anim/animator.hpp"
#include "anim/battle_animations.hpp"
#include "anim/unit_animation.hpp"
#include "map/map_objects.hpp"
#include "map/map_renderer.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace openday;
using namespace openday::game;

namespace {

int failures = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

UnitType testUnit(int race) {
    UnitType t;
    t.image = "HTRO";
    t.race = race;
    t.clips.fill(true);
    t.clips[static_cast<std::size_t>(UnitAction::MoveStart)] = false;
    t.movePixSpeed = 8;
    t.projectileOffset = 10;
    t.projectileSpeed = 20;
    t.ruinImage = "UHRUI210";
    return t;
}

void testNames() {
    const UnitType human = testUnit(0), tauran = testUnit(1);
    CHECK(unitClipName(human, 2, UnitAction::ShootBegin, 6) == "HTRO\\P2\\HTRO2S6.SMK");
    CHECK(unitClipName(human, 0, UnitAction::Move, 3) == "HTRO\\P1\\HTRO1M3.SMK");  // human color 0 -> 1
    CHECK(unitClipName(tauran, 0, UnitAction::Move, 3) == "HTRO\\P0\\HTRO0M3.SMK");
    CHECK(unitClipName(human, 3, UnitAction::Explosion, 5) == "HTRO\\P1\\HTRO1E0.SMK"); // single clips: P0/P1, facing 0
    CHECK(unitClipName(tauran, 0, UnitAction::Impact, 2) == "HTRO\\P0\\HTRO0I0.SMK");
    CHECK(unitClipName(human, 1, UnitAction::MoveStart, 0).empty());
    BuildingType b{"HBAR", 2, 2, 100, false, true};
    CHECK(buildingClipName(b) == "BUILDING\\HBAR.SMK");
    b.hasExplosion = false;
    CHECK(buildingClipName(b).empty());
}

void testFacings() {
    struct Step {
        int dx, dy, facing;
    };
    const Step steps[] = {{0, 1, 0}, {-1, 1, 1}, {-1, 0, 2}, {-1, -1, 3}, {0, -1, 4}, {1, -1, 5}, {1, 0, 6}, {1, 1, 7}};
    for (const Step& s : steps) {
        CHECK(facingOfStep(s.dx, s.dy) == s.facing);
        CHECK(facingToward({5, 5}, {5 + s.dx, 5 + s.dy}) == s.facing);
        CHECK(facingToward({5, 5}, {5 + 4 * s.dx, 5 + 4 * s.dy}) == s.facing);
    }
}

void testPlacement() {
    const int n = 10;
    const Point o = iso::tileOrigin(3, 4, n);
    const Point p = framePosition(3, 4, n, 216, 224);
    CHECK(p.x == o.x - 81 && p.y == o.y - 70);
    const Point big = framePosition(3, 4, n, 432, 448);
    CHECK(big.x == o.x - 189 && big.y == o.y - 182);
    // Building explosions center on the footprint.
    const Point b2 = buildingFramePosition(3, 4, 2, 2, n, 100, 100);
    CHECK(b2.x == o.x + 27 - 50 && b2.y == o.y + 42 - 50);
    const Point b3 = buildingFramePosition(3, 4, 3, 3, n, 100, 100);
    CHECK(b3.x == o.x + 27 - 50 && b3.y == o.y + 56 - 50);
}

void testPaths() {
    const int n = 10;
    // Orthogonal step at 8 px/tick: floor(27 / 8) = 3 ticks; the "long"
    // diagonal (56 px across): floor(55 / 8) = 6 ticks.
    const auto ortho = slidePositions({{2, 2}, {3, 2}}, 8, n, 216, 224);
    CHECK(ortho.size() == 3);
    const auto diag = slidePositions({{2, 2}, {3, 1}}, 8, n, 216, 224);
    CHECK(diag.size() == 6);
    if (!ortho.empty()) {
        const Point a = framePosition(2, 2, n, 216, 224);
        CHECK(ortho[0].pos.x == a.x + 8 && ortho[0].pos.y == a.y + 4);
        CHECK(ortho[0].tile.x == 3 && ortho[0].tile.y == 2 && ortho[0].facing == 6);
    }
    const auto path = straightPath({0, 0}, {3, 1});
    CHECK(path.size() == 4 && path[1].x == 1 && path[1].y == 1 && path[3].x == 3 && path[3].y == 1);

    UnitType t = testUnit(0);
    CHECK(projectilePath({2, 2}, {3, 3}, t, n, 20, 20).empty()); // diagonal neighbor
    // Three tiles along x: 84 px across, 85 points; skip 10, then every 20 px.
    const auto proj = projectilePath({0, 0}, {3, 0}, t, n, 20, 20);
    CHECK(proj.size() == 4);
    if (proj.size() == 4) {
        const Point o = iso::tileOrigin(0, 0, n);
        CHECK(proj[0].x == o.x + 27 + 10 - 10 && proj[0].y == o.y + 14 + 5 - 10);
        CHECK(proj[3].x - proj[0].x == 60);
    }
    // An arc lifts the middle of the path.
    t.projectileCurve = 20;
    const auto arc = projectilePath({0, 0}, {3, 0}, t, n, 20, 20);
    CHECK(arc.size() >= 3);
    if (arc.size() >= 3) CHECK(arc[arc.size() / 2].y < proj[proj.size() / 2].y);
}

render::Animation testClip(int frames) {
    render::Animation a;
    a.width = 216;
    a.height = 224;
    for (int i = 0; i < frames; ++i) {
        render::AnimFrame f;
        f.left = 100;
        f.top = 90;
        f.sprite.width = f.sprite.height = 1;
        f.sprite.pixels = {static_cast<std::uint8_t>(i)};
        f.sprite.opaque = {1};
        a.frames.push_back(f);
    }
    return a;
}

void testAnimator() {
    const render::Animation clip = testClip(3);
    Animator anim;
    std::vector<std::string> log;
    AnimTrack t;
    t.clips[0] = &clip;
    t.duration = 2;
    t.pos = {10, 20};
    t.tile = {1, 1};
    t.onStart = [&] { log.push_back("start@" + std::to_string(anim.now())); };
    t.onEnd = [&] { log.push_back("end@" + std::to_string(anim.now())); };
    CHECK(anim.add(t) == 3);
    std::vector<int> shown;
    for (int i = 0; i < 5; ++i) {
        anim.tick();
        for (const AnimDraw& ov : anim.frames()) {
            shown.push_back(ov.sprite->pixels[0]);
            CHECK(ov.x == 110 && ov.y == 110 && ov.refresh.empty());
        }
    }
    CHECK(shown == std::vector<int>({0, 1, 2}));
    // The end action runs at the next tick, after the last frame was shown.
    CHECK(log == std::vector<std::string>({"start@1", "end@3"}));
    CHECK(anim.idle());
}

struct TestMap {
    UnitType type = testUnit(0);
    GlobalRules rules;
    MapEntities entities;
    MapScene scene{TerrainMap{}, {}};

    TestMap() {
        TerrainMap t;
        t.size = 6;
        t.tiles.assign(36, "GRASS0");
        rules.terrain["GRASS0"].cls = TerrainClass::Plain;
        entities.units.push_back({&type, 2, 0, 1, 1});
        scene = MapScene(t, {unitSprite(entities.units[0], 0, t, rules, false)});
    }
};

std::string unitName(const MapScene& scene) {
    for (const MapSprite& s : scene.objects())
        if (s.layer == Layer::Unit) return std::to_string(s.x) + "," + std::to_string(s.y) + ":" + s.name;
    return "none";
}

void testBattleAnimations() {
    TestMap m;
    BattleAnimations battle(m.scene, m.entities, m.rules, nullptr); // no clips: actions still run
    CHECK(unitName(m.scene) == "1,1:HTRO2000");
    battle.move(0, straightPath({1, 1}, {3, 4}));
    for (int i = 0; i < 10 && battle.busy(); ++i) battle.tick();
    CHECK(!battle.busy());
    // Last step (3,3) -> (3,4) faces down-left (0); the unit is on its target.
    CHECK(unitName(m.scene) == "3,4:HTRO2000");
    CHECK(battle.unitAt({3, 4}) == 0 && battle.unitAt({1, 1}) == -1);

    battle.attack(0, {3, 1});
    for (int i = 0; i < 10 && battle.busy(); ++i) battle.tick();
    CHECK(unitName(m.scene) == "3,4:HTRO2400"); // turned toward the target (up-right)

    battle.destroyUnit(0);
    for (int i = 0; i < 10 && battle.busy(); ++i) battle.tick();
    CHECK(unitName(m.scene) == "none");
    CHECK(m.scene.hasObject(3, 4, Layer::Ruin));
    CHECK(battle.unitAt({3, 4}) == -1);
}

void testHiddenWhileAnimating() {
    TestMap m;
    BattleAnimations battle(m.scene, m.entities, m.rules, nullptr);
    auto unitHidden = [&] {
        const SceneExtras e = battle.extras();
        for (const MapSprite& s : m.scene.objects())
            if (s.layer == Layer::Unit) return e.hidden(s);
        return false;
    };
    CHECK(!unitHidden());
    battle.destroyUnit(0);
    battle.tick(); // without a clip the end action removes the unit at once
    CHECK(!m.entities.units[0].alive && unitName(m.scene) == "none" && !battle.busy());
}

// A part without a clip after one that showed frames (a move without an end
// clip): its end action runs in its own tick, so no tick shows neither the
// clip nor the static sprite.
void testEmptyEndPart() {
    const render::Animation clip = testClip(2);
    Animator anim;
    bool hidden = true;
    AnimTrack slide;
    slide.clips[0] = &clip;
    slide.duration = 1;
    const int slideEnd = anim.add(slide);
    AnimTrack end;
    end.start = slideEnd + 1;
    end.onEnd = [&] { hidden = false; };
    anim.add(end);
    for (int i = 0; i < 4; ++i) {
        anim.tick();
        CHECK(!anim.frames().empty() || !hidden);
    }
    CHECK(!hidden && anim.idle());
}

bool hasCell(const std::vector<RefreshCell>& list, RefreshCell c) {
    for (const RefreshCell& r : list)
        if (r.x == c.x && r.y == c.y && r.rx == c.rx && r.ry == c.ry && r.w == c.w && r.h == c.h) return true;
    return false;
}

void testOcclusionTables() {
    const std::vector<RefreshCell> rest = refreshAtRest({4, 5});
    CHECK(rest.size() == 5);
    CHECK(hasCell(rest, {4, 6, 28, 0, 27, 42}) && hasCell(rest, {5, 6, 0, 0, 54, 28}) && hasCell(rest, {5, 7, 28, 0, 27, 14}));
    CHECK(hasCell(rest, {5, 5, 0, 0, 27, 42}) && hasCell(rest, {6, 6, 0, 0, 27, 14}));
    // Facings 0 and 4 share the table of A = the tile with the smaller y.
    const std::vector<RefreshCell> f0 = refreshTranslate({4, 5}, 0), f4 = refreshTranslate({4, 4}, 4);
    CHECK(f0.size() == 7 && hasCell(f0, {5, 4, 0, 0, 27, 56}) && hasCell(f0, {5, 7, 28, 0, 27, 14}));
    CHECK(hasCell(f4, {5, 4, 0, 0, 27, 56}) && hasCell(f4, {6, 6, 0, 0, 54, 14}));
    // Facings 2 and 6: F = the tile with the smaller x.
    const std::vector<RefreshCell> f6 = refreshTranslate({4, 5}, 6), f2 = refreshTranslate({3, 5}, 2);
    CHECK(hasCell(f6, {3, 6, 28, 0, 27, 56}) && hasCell(f6, {6, 6, 0, 0, 27, 14}) && hasCell(f2, {5, 5, 0, 0, 27, 42}));
    // Facings 1 and 5: C = the tile with the smaller x.
    const std::vector<RefreshCell> f5 = refreshTranslate({4, 5}, 5), f1 = refreshTranslate({3, 6}, 1);
    CHECK(f5.size() == 8 && hasCell(f5, {5, 5, 0, 0, 27, 42}) && hasCell(f5, {6, 6, 0, 0, 27, 14}));
    CHECK(hasCell(f1, {3, 7, 28, 0, 27, 42}));
    // Facings 3 and 7: the at-rest table of the tile with the larger x and y.
    const std::vector<RefreshCell> f3 = refreshTranslate({4, 5}, 3), f7 = refreshTranslate({5, 6}, 7);
    CHECK(f3.size() == 5 && hasCell(f3, {5, 7, 28, 0, 27, 42}) && hasCell(f7, {5, 7, 28, 0, 27, 42}));
}

void testFrameOrder() {
    const render::Animation clip = testClip(3);
    Animator anim;
    auto track = [&](int group, Point pos, Occlusion occ) {
        AnimTrack t;
        t.clips[0] = &clip;
        t.duration = 2;
        t.pos = pos;
        t.tile = {2, 2};
        t.group = group;
        t.occlusion = occ;
        anim.add(t);
    };
    track(1, {0, 0}, Occlusion::None);         // floating: after every ground frame
    track(0, {50, 40}, Occlusion::AtRest);     // lower: after the next one
    track(0, {90, 10}, Occlusion::AtRest);     // higher on screen
    track(0, {20, 10}, Occlusion::None);       // same top, further left: first
    anim.tick();
    const std::vector<AnimDraw>& f = anim.frames();
    CHECK(f.size() == 4);
    if (f.size() == 4) {
        CHECK(f[0].x == 120 && f[1].x == 190 && f[2].x == 150 && f[3].x == 100);
        CHECK(f[1].refresh.size() == 5 && f[0].refresh.empty() && f[3].refresh.empty());
    }
}

void testFacingRestart() {
    // A move clip per facing; a facing change starts the new clip at frame 0
    // even when it was shown before.
    const render::Animation clip = testClip(5);
    Animator anim;
    AnimTrack t;
    t.loop = true;
    t.clips.fill(&clip);
    const int facings[] = {0, 0, 0, 2, 2, 0, 0};
    for (const int f : facings) t.path.push_back({{0, 0}, {1, 1}, f});
    t.duration = static_cast<int>(t.path.size()) - 1;
    t.occlusion = Occlusion::Translate;
    anim.add(t);
    std::vector<int> shown;
    for (int i = 0; i < 7; ++i) {
        anim.tick();
        for (const AnimDraw& d : anim.frames()) shown.push_back(d.sprite->pixels[0]);
    }
    CHECK(shown == std::vector<int>({0, 1, 2, 0, 1, 0, 1}));
}

void testStaggeredMoves() {
    // Two units of a group order: the second one sets off 3 ticks later
    // (3 x (record id - 1)), not after the first one's move.
    TestMap m;
    m.entities.units.push_back({&m.type, 2, 0, 4, 1});
    m.scene.replaceEntity(Layer::Unit, 1, {unitSprite(m.entities.units[1], 1, m.scene.terrain(), m.rules, false)});
    BattleAnimations battle(m.scene, m.entities, m.rules, nullptr);
    std::vector<std::string> log;
    battle.beginPhase(true);
    battle.nextRecord();
    battle.move(0, straightPath({1, 1}, {1, 3}));
    battle.nextRecord();
    battle.move(1, straightPath({4, 1}, {4, 3}));
    // Without clips each move takes no time: unit 0 arrives first, unit 1
    // three ticks later.
    int arrived0 = -1, arrived1 = -1;
    for (int i = 0; i < 12 && battle.busy(); ++i) {
        battle.tick();
        if (arrived0 < 0 && m.entities.units[0].y == 3) arrived0 = battle.animator().now();
        if (arrived1 < 0 && m.entities.units[1].y == 3) arrived1 = battle.animator().now();
    }
    CHECK(arrived0 > 0 && arrived1 == arrived0 + 3);

    // Outside a staggered phase the second move waits for the first.
    battle.beginPhase(false);
    battle.nextRecord();
    battle.move(0, straightPath({1, 3}, {1, 4}));
    battle.nextRecord();
    battle.move(1, straightPath({4, 3}, {4, 4}));
    arrived0 = arrived1 = -1;
    for (int i = 0; i < 12 && battle.busy(); ++i) {
        battle.tick();
        if (arrived0 < 0 && m.entities.units[0].y == 4) arrived0 = battle.animator().now();
        if (arrived1 < 0 && m.entities.units[1].y == 4) arrived1 = battle.animator().now();
    }
    CHECK(arrived0 > 0 && arrived1 <= arrived0 + 1);
}

void testMineAndDamage() {
    TestMap m;
    BuildingType bt{"HBAR", 1, 1, 90, true, true};
    m.entities.buildings.push_back({&bt, 3, 3, true, 1, 90});
    m.scene.replaceEntity(Layer::Building, 0, buildingSprites(m.entities.buildings[0], 0, m.rules, m.scene.terrain()));
    m.scene.addObject({2, 2, Layer::Mine, SpriteSet::Mines, "MINE001"});
    BattleAnimations battle(m.scene, m.entities, m.rules, nullptr);
    auto buildingName = [&] {
        for (const MapSprite& s : m.scene.objects())
            if (s.layer == Layer::Building) return s.name;
        return std::string("none");
    };
    CHECK(buildingName() == "HBAR1000");
    battle.setBuildingArmor(0, 29); // a third or less (0.33 x 90 = 29.7): the damaged sprite at once
    battle.tick();
    CHECK(buildingName() == "HBAR1100");
    // Without a clip the mine explosion changes nothing on the map.
    MineType mine;
    mine.image = "MINE";
    battle.mineExplosion({2, 2}, mine, true);
    for (int i = 0; i < 5 && battle.busy(); ++i) battle.tick();
    CHECK(m.scene.hasObject(2, 2, Layer::Mine));
    CHECK(mineClipName(mine) == "MINE\\MINE.SMK");
    mine.hasExplosion = false;
    CHECK(mineClipName(mine).empty());
}

} // namespace

int main() {
    testNames();
    testFacings();
    testPlacement();
    testPaths();
    testAnimator();
    testBattleAnimations();
    testHiddenWhileAnimating();
    testEmptyEndPart();
    testOcclusionTables();
    testFrameOrder();
    testFacingRestart();
    testStaggeredMoves();
    testMineAndDamage();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("all anim tests passed");
    return EXIT_SUCCESS;
}
