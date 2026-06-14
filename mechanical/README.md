# mechanical/

Parametric OpenSCAD for the 2.45 GHz applicator front end: magnetron cooling,
magnetron-to-launcher mounting, and the launcher seating recess. Target tube is
the U-Solution / LG **2M246** (front-view spec: 127 x 93.2 envelope, 80 body,
Ø36.5 center boss, Ø23 bore, 4-Ø4.5 holes at 114.3 / 95 c-c x 35 c-c).

## Parts

### magtube_fan_adapter.scad
60 mm fan (AFC0612D, 4-wire PWM) -> the magnetron's open face. The tube's own
steel side walls are the duct; this just adapts the round fan throat to the
open-face aperture.
- Inner opening **93 (long) x 57 (tall)**, with a **5 mm skirt** that wraps the
  collar (`flange_depth` / `flange_wall`).
- Round-to-rectangle transition up to a straight 60 mm fan collar so the four
  fan screws land on a flat, exposed face (`fan_collar`).
- Blow-through: fan in one open face, exhaust the opposite open face (the tube).

### magtube_mount_plate.scad
Magnetron-side mounting plate mirroring the 2M246 flange.
- Outline is Meshy's photo reconstruction, sectioned + scaled to 127 mm, with
  the four corner paddle wings.
- Dimensioned features: Ø23 bore, Ø36.5 center-boss seating relief, 8 wing holes
  on the 114.3 / 95 x 35 pattern.
- `relief_depth` **[SET]** = boss protrusion height (sets how far the antenna
  seats); measure on the real tube.

### launcher_magnetron_recess.scad
Launcher-side **negative** so the magnetron nests into the launcher instead of
sitting proud, with a gasket pocket and antenna bore.
- `part="cutter"` — Boolean-Difference into an existing launcher (top ref at Z=0).
- `part="mount"`  — standalone block with the recess pre-cut.
- **[SET]** `flange_recess` (steel flange thickness), `gasket_od`, `gasket_depth`.

## Render
```
xvfb-run -a openscad -o part.stl -D part='"all"' mechanical/<file>.scad
```

## Pending measurements (the only non-spec numbers)
- `relief_depth` / `flange_recess` — magnetron flange thickness + boss protrusion
- `gasket_od` / `gasket_depth` — RF choke gasket pocket
- collar-wrap skirt fit (`flange_depth` 5 mm, `flange_wall` 3 mm) — confirm on bench;
  may need the skirt relieved on the two short (57) sides if the flange/terminal
  end crowds that edge.
