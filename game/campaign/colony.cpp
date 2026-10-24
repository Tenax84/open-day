#include "campaign/colony.hpp"

#include <algorithm>

namespace openday::game {
namespace {

int toInt(const data::Row& row, int col) { return row.isNull(col) ? 0 : static_cast<int>(row.integer(col)); }

} // namespace

const Province* Colony::province(int n) const {
    for (const Province& p : provinces)
        if (p.number == n) return &p;
    return nullptr;
}

bool Colony::areAdjacent(int a, int b) const {
    return std::any_of(adjacent.begin(), adjacent.end(), [&](const std::pair<int, int>& e) {
        return (e.first == a && e.second == b) || (e.first == b && e.second == a);
    });
}

Colony loadColony(const data::SqliteDb& db) {
    Colony c;
    db.forEach("SELECT \"No Continent\", NameEnglish, StartCash, PlayerCity, AlienCity, AiAutoUnitChanceCall, AiAutoUnitRes, "
               "Intro, \"End\", SmackProvinces, SmackProvFrameBegin, SmackProvFrameQty, DeployEasy, DeployAvg, DeployHard "
               "FROM \"Continent Info\"",
               [&](const data::Row& r) {
                   c.number = toInt(r, 0) + 1;
                   c.name = r.text(1);
                   c.startCash = toInt(r, 2);
                   c.playerCity = toInt(r, 3);
                   c.alienCity = toInt(r, 4);
                   c.aiAutoChance = toInt(r, 5);
                   c.aiAutoBudget = toInt(r, 6);
                   c.intro = r.text(7);
                   c.end = r.text(8);
                   c.animation = r.text(9);
                   c.animationFirst = toInt(r, 10);
                   c.animationFrames = toInt(r, 11);
                   c.deployPercent = {toInt(r, 12), toInt(r, 13), toInt(r, 14)};
               });
    db.forEach("SELECT NoProvince, ProvinceNameEnglish, TitleEnglish, DescriptionEnglish, ObjectiveEnglish, MapSize, "
               "Credits, Tech, mapScreen1, mapScreen2, mapScreen3, mapScreen4 FROM \"Province List\" ORDER BY NoProvince",
               [&](const data::Row& r) {
                   Province p;
                   p.number = toInt(r, 0);
                   p.name = r.text(1);
                   p.title = r.text(2);
                   p.description = r.text(3);
                   p.objective = r.text(4);
                   p.mapSize = toInt(r, 5);
                   p.credits = toInt(r, 6);
                   p.tech = toInt(r, 7);
                   for (int k = 0; k < 4; ++k) p.mapScreen[static_cast<std::size_t>(k)] = r.blob(8 + k);
                   c.provinces.push_back(std::move(p));
               });
    if (db.hasTable("Province List Captured"))
        db.forEach("SELECT NoProvince, Captured, RandomChosen FROM \"Province List Captured\"", [&](const data::Row& r) {
            for (Province& p : c.provinces)
                if (p.number == toInt(r, 0)) {
                    p.captured = r.integer(1) != 0;
                    p.variant = std::max(1, toInt(r, 2));
                }
        });
    if (db.hasTable("Adjacent Provinces"))
        db.forEach("SELECT ProvinceNo, AdjacenTo FROM \"Adjacent Provinces\" ORDER BY Counter",
                   [&](const data::Row& r) { c.adjacent.emplace_back(toInt(r, 0), toInt(r, 1)); });
    if (db.hasTable("Buildings Available"))
        db.forEach("SELECT buildingType FROM \"Buildings Available\" ORDER BY Counter",
                   [&](const data::Row& r) { c.buildingsAvailable.push_back(toInt(r, 0)); });
    if (db.hasTable("AI automated available"))
        db.forEach("SELECT Enum FROM \"AI automated available\" ORDER BY Counter",
                   [&](const data::Row& r) { c.aiAutomated.push_back(toInt(r, 0)); });
    return c;
}

} // namespace openday::game
