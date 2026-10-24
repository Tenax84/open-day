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
        for (const Overlay& ov : anim.overlays()) {
            shown.push_back(ov.sprite->pixels[0]);
            CHECK(ov.x == 110 && ov.y == 110 && ov.tileX == 1 && ov.tileY == 1);
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
        rules.terrain = {{"GRASS0", {TerrainClass::Plain}}};
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
    battle.tick(); // the start action hides the static sprite
    CHECK(unitHidden() && m.entities.units[0].alive);
    battle.tick(); // the end action removes the unit
    CHECK(!m.entities.units[0].alive && unitName(m.scene) == "none");
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
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("all anim tests passed");
    return EXIT_SUCCESS;
}
