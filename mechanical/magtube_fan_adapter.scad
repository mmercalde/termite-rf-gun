// ============================================================================
// magtube_fan_adapter.scad
// AFC0612D 60mm fan -> magnetron open face.
// Inner opening 93 long (X) x 57 tall (Y) that WRAPS the magnetron collar via a
// 5mm skirt, then ducts up to the 60mm fan with a straight mounting collar.
// Z axis = airflow; collar end at Z=0, fan at top.
// ============================================================================
$fn = 72;
part = "all";        // "all" | "fitcheck"

/* opening / collar wrap */
op_w         = 93;   // inner opening LONG (X) - wraps collar
op_h         = 57;   // inner opening TALL (Y) - wraps collar
flange_depth = 5;    // skirt depth that slips over the collar
flange_wall  = 3;    // skirt wall thickness

/* fan AFC0612D */
fan_size=60; fan_hole_sq=50; fan_pilot=3.2; fan_bore=56; boss_d=9; boss_h=6; fan_collar=9;

/* duct */
wall=3; duct_len=20;

sk_w = op_w + 2*flange_wall;   // skirt/duct outer
sk_h = op_h + 2*flange_wall;

// 5mm skirt that wraps the collar (straight walls, open bottom)
module skirt(){
  linear_extrude(flange_depth)
    difference(){ square([sk_w,sk_h],center=true); square([op_w,op_h],center=true); }
}
// duct: op_w x op_h opening -> 60 round fan
module duct(){
  translate([0,0,flange_depth])
  difference(){
    hull(){
      linear_extrude(0.1) square([sk_w,sk_h],center=true);
      translate([0,0,duct_len]) linear_extrude(0.1) square([fan_size,fan_size],center=true);
    }
    hull(){
      translate([0,0,-1]) linear_extrude(1.1) square([op_w,op_h],center=true);
      translate([0,0,duct_len+0.1]) linear_extrude(0.1) circle(d=fan_bore);
    }
  }
}
// straight fan collar + plate (flat, accessible holes)
module fan_mount(){
  z0 = flange_depth + duct_len;
  translate([0,0,z0]) difference(){
    linear_extrude(fan_collar) square([fan_size,fan_size],center=true);
    translate([0,0,-1]) linear_extrude(fan_collar+2) circle(d=fan_bore);
  }
  translate([0,0,z0+fan_collar]) difference(){
    linear_extrude(wall) square([fan_size,fan_size],center=true);
    translate([0,0,-1]) cylinder(d=fan_bore,h=wall+2);
    for(sx=[-1,1],sy=[-1,1]) translate([sx*fan_hole_sq/2,sy*fan_hole_sq/2,-1]) cylinder(d=fan_pilot,h=wall+2);
  }
  translate([0,0,z0]) for(sx=[-1,1],sy=[-1,1])
    translate([sx*fan_hole_sq/2,sy*fan_hole_sq/2,fan_collar-boss_h]) difference(){
      cylinder(d=boss_d,h=boss_h+wall);
      translate([0,0,-1]) cylinder(d=fan_pilot,h=boss_h+wall+2);
    }
}
module adapter(){ skirt(); duct(); fan_mount(); }
module collar_ghost(){ color([.7,.7,.7,.35]) translate([0,0,-30]) linear_extrude(34) square([op_w,op_h],center=true); }

if(part=="all")      adapter();
if(part=="fitcheck"){ adapter(); collar_ghost(); }
