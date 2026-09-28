// hud_rail_mount.scad - concept housing for the TAK HUD + FLIR Boson on a
// Picatinny rail. Units: mm. Print in PETG/ASA/nylon for fit checks only; a
// fielded version should be machined aluminium.
//
// Coordinate system: X = forward (towards the muzzle), Y = left, Z = up,
// rail top surface at Z = 0.

$fn = 48;

/* [Picatinny] */
rail_width      = 21.2;   // MIL-STD-1913 top width
slot_width      = 5.23;
slot_pitch      = 10.01;
clamp_length    = 40;     // along the rail (4 slots)
lug_count       = 1;

/* [Housing] */
wall            = 2.5;
body_len        = 150;
body_w          = 44;
body_h          = 46;
body_z0         = 12;     // underside of body above rail top

/* [Boson 640 + 50 deg lens] */
boson_core      = 21.5;   // 21 x 21 mm engine, with clearance
boson_depth     = 30;     // core + short lens, approximate: check the STEP file
lens_dia        = 16;

/* [Battery bay] */
batt_l = 70; batt_w = 36; batt_h = 10;

/* [Eyepiece] */
eyepiece_dia    = 26;
eyepiece_len    = 30;

module picatinny_clamp() {
    // Base block with a 45 deg dovetail channel that fits over the rail.
    difference() {
        translate([-clamp_length/2, -(rail_width/2 + 6), 0])
            cube([clamp_length, rail_width + 12, body_z0]);
        // Dovetail channel (approximate 1913 profile)
        translate([-clamp_length/2 - 1, 0, 0])
            rotate([0, 90, 0])
                linear_extrude(clamp_length + 2)
                    polygon([[0, -rail_width/2], [0, rail_width/2],
                             [-3.0, rail_width/2 - 3.0 + 3.0], [-3.0, -(rail_width/2)]]);
        // Rail body clearance
        translate([-clamp_length/2 - 1, -rail_width/2, -10])
            cube([clamp_length + 2, rail_width, 10]);
        // Cross-bolt holes (M5), one per lug position
        for (i = [0 : lug_count - 1])
            translate([-(lug_count - 1) * slot_pitch / 2 + i * slot_pitch, 0, 3])
                rotate([90, 0, 0]) cylinder(d = 5.3, h = rail_width + 20, center = true);
    }
    // Recoil lug that sits in a rail slot
    for (i = [0 : lug_count - 1])
        translate([-(lug_count - 1) * slot_pitch / 2 + i * slot_pitch - (slot_width - 0.2) / 2,
                   -rail_width/2, -2.5])
            cube([slot_width - 0.2, rail_width, 2.5]);
}

module housing() {
    difference() {
        translate([-body_len/2, -body_w/2, body_z0])
            minkowski() {
                cube([body_len, body_w, body_h]);
                sphere(r = 0.01);
            }
        // Hollow
        translate([-body_len/2 + wall, -body_w/2 + wall, body_z0 + wall])
            cube([body_len - 2*wall, body_w - 2*wall, body_h]);
        // Boson lens aperture (front face)
        translate([body_len/2 - 5, 0, body_z0 + body_h/2 + 4])
            rotate([0, 90, 0]) cylinder(d = lens_dia + 1, h = 20);
        // Eyepiece aperture (rear face)
        translate([-body_len/2 - 5, 0, body_z0 + body_h/2 + 4])
            rotate([0, 90, 0]) cylinder(d = eyepiece_dia - 4, h = 20);
        // USB-C port (rear, low)
        translate([-body_len/2 - 1, -5, body_z0 + wall + 2]) cube([6, 10, 4]);
    }
}

module boson_bay() {
    // Rigid cradle for the Boson engine, bolted to the clamp through the floor
    translate([body_len/2 - wall - boson_depth, -boson_core/2 - 2, body_z0 + wall])
        difference() {
            cube([boson_depth, boson_core + 4, body_h/2 + 4 + boson_core/2 + 2]);
            translate([-1, 2, body_h/2 + 4 - boson_core/2]) cube([boson_depth + 2, boson_core, boson_core]);
        }
}

module battery_bay() {
    %translate([-batt_l/2 - 10, -batt_w/2, body_z0 + wall]) cube([batt_l, batt_w, batt_h]);
}

module electronics_ghost() {
    // ESP32 board and backpack
    %translate([-20, -16, body_z0 + wall + batt_h + 3]) cube([40, 32, 8]);
    // Magnetometer marker (keep high and rearward)
    %translate([-45, 0, body_z0 + body_h - 4]) cube([4, 4, 1], center = true);
}

module eyepiece() {
    translate([-body_len/2 - eyepiece_len, 0, body_z0 + body_h/2 + 4])
        rotate([0, 90, 0])
            difference() {
                cylinder(d = eyepiece_dia, h = eyepiece_len);
                translate([0, 0, -1]) cylinder(d = eyepiece_dia - 4, h = eyepiece_len + 2);
            }
}

// Rail stub for visualisation
%translate([-80, -rail_width/2, -8]) cube([160, rail_width, 8]);

picatinny_clamp();
housing();
boson_bay();
battery_bay();
electronics_ghost();
eyepiece();
