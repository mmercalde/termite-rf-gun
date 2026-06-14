// ============================================================================
// launcher_magnetron_recess.scad
// Negative of the 2M246 flange for the LAUNCHER side: flange-shaped recess so
// the magnetron nests in, a deeper gasket pocket (RF choke), and the antenna
// bore through to the cavity. Holes aligned to the flange pattern.
//   part="cutter" : Boolean-Difference this into your launcher (top face at z=0)
//   part="mount"  : standalone launcher block with the recess already cut
// ============================================================================
$fn=120;
part = "mount";        // "cutter" | "mount"

/* recess geometry  [SET]=measure on the real magnetron */
flange_recess = 2.5;   // [SET] real magnetron flange thickness + clearance (how deep it nests)
fit_clear     = 0.4;   // XY drop-in clearance around the flange
gasket_od     = 52;    // [SET] RF choke gasket outer dia
gasket_depth  = 3.0;   // [SET] gasket pocket depth
antenna_dia   = 25;    // antenna bore to the cavity
hole_dia      = 4.6;   // bolt clearance through the launcher
h_outer=114.3; h_inner=95.0; v_cc=35.0;
deep = 60;             // bore/hole cut-through depth (>= launcher wall)

/* standalone block (part="mount" only) */
mount_w=150; mount_h=120; mount_t=15;

outline=[
  [-63.63, -13.77],
  [-61.70, -10.17],
  [-58.92, -8.86],
  [-47.83, -9.61],
  [-44.81, -7.79],
  [-43.94, -6.64],
  [-43.14, -4.21],
  [-43.29, 4.48],
  [-44.71, 7.68],
  [-45.59, 8.48],
  [-47.86, 9.50],
  [-58.82, 8.68],
  [-61.38, 9.72],
  [-63.44, 12.78],
  [-63.12, 20.98],
  [-62.74, 21.73],
  [-58.98, 24.48],
  [-43.24, 23.89],
  [-41.51, 24.70],
  [-40.87, 26.27],
  [-40.07, 45.91],
  [-39.28, 46.79],
  [-34.88, 46.81],
  [-33.21, 45.51],
  [-32.00, 45.20],
  [29.69, 45.03],
  [32.67, 45.29],
  [34.72, 46.81],
  [38.68, 46.78],
  [39.37, 46.00],
  [40.77, 26.32],
  [41.47, 24.77],
  [43.30, 24.01],
  [59.37, 24.31],
  [61.76, 22.90],
  [63.23, 20.49],
  [63.39, 13.23],
  [60.58, 9.37],
  [47.36, 9.37],
  [45.19, 8.29],
  [43.28, 5.11],
  [43.10, -4.57],
  [44.06, -7.28],
  [47.18, -9.49],
  [56.78, -8.80],
  [60.60, -9.45],
  [63.24, -12.90],
  [63.36, -20.14],
  [62.28, -22.65],
  [59.10, -24.59],
  [42.97, -24.17],
  [41.27, -24.98],
  [40.62, -26.52],
  [39.54, -46.29],
  [38.85, -47.09],
  [34.65, -47.11],
  [33.09, -45.81],
  [31.90, -45.51],
  [-31.30, -45.33],
  [-33.27, -45.70],
  [-34.89, -47.09],
  [-39.08, -46.93],
  [-39.76, -46.04],
  [-40.83, -26.54],
  [-41.33, -25.16],
  [-43.14, -24.17],
  [-58.93, -24.63],
  [-61.16, -23.79],
  [-63.27, -21.10]
];

module holes2d(){
  for(sx=[-1,1],sy=[-1,1]){
    translate([sx*h_outer/2, sy*v_cc/2]) circle(d=hole_dia);
    translate([sx*h_inner/2, sy*v_cc/2]) circle(d=hole_dia);
  }
}

// negative volume: top reference at z=0, everything cuts downward (-Z)
module cutter(){
  union(){
    translate([0,0,-flange_recess]) linear_extrude(flange_recess+0.2) offset(r=fit_clear) polygon(outline); // flange recess
    translate([0,0,-(flange_recess+gasket_depth)]) cylinder(d=gasket_od, h=gasket_depth+0.2);                // gasket pocket
    translate([0,0,-deep]) cylinder(d=antenna_dia, h=deep+0.2);                                              // antenna bore
    translate([0,0,-deep]) linear_extrude(deep+0.2) holes2d();                                               // bolt holes
  }
}

module mount(){
  difference(){
    linear_extrude(mount_t) offset(r=6) offset(delta=-6) square([mount_w,mount_h],center=true);
    translate([0,0,mount_t]) cutter();   // cutter top at the block top face
  }
}

if(part=="cutter") cutter();
if(part=="mount")  mount();
