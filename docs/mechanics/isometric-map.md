# Isometric map geometry

How the terrain of a scenario map (`Map` table, see
[data-model.md](data-model.md)) is laid out on screen. Everything here was
measured from the sprite masks in `TERRAINS.DBI` and checked by rendering all
114 shipped scenario maps. The coastlines, climate transitions and
multi-tile features line up without seams in every map.

## Tile sprite

Terrain sprites are 54 × 56 pixels. The flat ground of a tile is a diamond in
the **bottom half** of the sprite:

- Rows 28–55 (28 rows). The diamond is 2 px wide at row 28, grows by 2 px on
  each side per row until it is 54 px wide at rows 41 and 42, and then shrinks
  the same way.
- Rows 0–27 are empty for flat tiles. Tall features such as trees, rocks and
  mountains use them and reach up over the tiles behind them.
  `Terrain Types.Overlapping` marks such tiles.

## Grid to screen

The diamonds tile the plane exactly, with no gaps or overlap, at a step of
**28 px horizontally and 14 px vertically**:

- `+1` in `PosX` moves the tile 28 px right and 14 px down.
- `+1` in `PosY` moves the tile 28 px left and 14 px down.

With tile (0, 0) at the top corner of the map, the top-left corner of tile
(x, y)'s sprite in an n × n map is:

```
screen_x = (x − y + n − 1) × 28
screen_y = (x + y) × 14
```

The whole map then measures `(2n − 2) × 28 + 54` by `(2n − 2) × 14 + 56`
pixels, e.g. 5374 × 2716 for a 96 × 96 map.

This orientation matches the scenario's top-down overview image
(`Scenario Info.mapScreen640`), where `PosX` runs to the right and `PosY`
downward. The isometric view is that image rotated 45° clockwise and
squashed vertically by half. *(?)*: whether the original program puts tile
(0, 0) at the top corner or uses a different screen origin does not matter
for the image. It only affects scrolling limits.

## Draw order

Tiles are drawn back to front by depth `x + y`. Within one depth, the order
does not matter, because tiles of the same depth never overlap. Tall
sprites then correctly cover the ground of the tiles behind them.

## Border outside the map

The original also draws the cells up to **2 tiles outside the map** on
every side, with the terrain sprite `BLACK`, so the map is framed by black
diamonds when scrolled to its edge. A cell inside the map whose terrain is
not set falls back to `BORE000`.

## Overlapping terrain

Each map cell keeps the `Terrain Types.Overlapping` flag of its terrain.
It decides whether the terrain sprite has to be redrawn when animations
are repaired for occlusion ([unit-animation.md](unit-animation.md) 8): only
tall (overlapping) terrain can cover an animation. When a tile's terrain
changes during a battle (burned terrain), the new sprite and its flag are
written into the cell and the map is redrawn.

## Palette

Sprites are composed in 8-bit palette indices. The final colors come from
the main palette `INTERF\PAL.BMP`, with the color cycling of
[PALCYCLE.md](../formats/PALCYCLE.md) applied on top. Water and shore
tiles use the cycled entries 239–241.
