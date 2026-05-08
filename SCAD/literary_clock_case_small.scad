// =============================================================================
// Literary Clock Case — Elecrow CrowPanel 2.13" + MakerFocus 1000mAh LiPo
// =============================================================================

// ---- BOARD ----
PCB_W           = 66.0;
PCB_H           = 32.0;
PCB_STACK       = 12.0;

// ---- SCREEN WINDOW ----
SCREEN_W        = 49.5;
SCREEN_H        = 26.0;
SCREEN_LEFT     = 9.5;
SCREEN_TOP      = 3.0;

// ---- USB-C ----
USB_W           = 14.0;
USB_H           = 6.5;

// ---- BATTERY ----
BAT_L           = 50.0;
BAT_W           = 30.0;
BAT_T           = 8.0;

// ---- CASE ----
WALL            = 2.8;
FLOOR           = 2.5;
LID_T           = 2.5;
CORNER_R        = 4.0;
TOLERANCE       = 0.25;
LIP_H           = 3.0;
LIP_T           = 1.2;
BAT_PAD         = 2.0;
PCB_PAD         = 1.0;
SHELF_W         = 3.5;   // how far shelf protrudes inward

// ---- DERIVED ----
INNER_W         = PCB_W + PCB_PAD * 2;
INNER_H         = PCB_H + PCB_PAD * 2;
BAT_BAY_H       = BAT_T + BAT_PAD;
PCB_SHELF_Z     = FLOOR + BAT_BAY_H + 1.0;
BASE_DEPTH      = PCB_SHELF_Z + PCB_STACK;
OUTER_W         = INNER_W + WALL * 2;
OUTER_H         = INNER_H + WALL * 2;

USB_Z_CENTER    = PCB_SHELF_Z + PCB_STACK / 2;
USB_Z_BOTTOM    = USB_Z_CENTER - USB_H / 2;
USB_Z_TOP       = USB_Z_CENTER + USB_H / 2;

$fn = 64;

// =============================================================================
// UTILITIES
// =============================================================================
module rounded_box(w, h, d, r) {
    hull() {
        for (x = [r, w - r])
            for (y = [r, h - r])
                translate([x, y, 0])
                    cylinder(r=r, h=d);
    }
}

// Shelf bracket along X axis, length l, protruding in +Y by SHELF_W.
// Top face is at z=0 (caller translates to PCB_SHELF_Z).
// 45° chamfer on underside: hull between top rectangle and a zero-height
// line at the base of the wall (y=0, z=-SHELF_W).
module shelf_x(l) {
    hull() {
        // Top face: the ledge the PCB sits on
        translate([0, 0, 0])
            cube([l, SHELF_W, 0.01]);
        // Bottom edge: a line at the wall face, SHELF_W below the top
        translate([0, 0, -SHELF_W])
            cube([l, 0.01, 0.01]);
    }
}

// Same but along Y axis
module shelf_y(l) {
    hull() {
        translate([0, 0, 0])
            cube([SHELF_W, l, 0.01]);
        translate([0, 0, -SHELF_W])
            cube([0.01, l, 0.01]);
    }
}

// =============================================================================
// BASE
// =============================================================================
module base() {
    difference() {
        rounded_box(OUTER_W, OUTER_H, BASE_DEPTH, CORNER_R);

        // Inner cavity
        translate([WALL, WALL, FLOOR])
            cube([INNER_W, INNER_H, BASE_DEPTH]);

        // Battery recess
        BAT_X = WALL + (INNER_W - BAT_L) / 2;
        BAT_Y = WALL + (INNER_H - BAT_W) / 2;
        translate([BAT_X, BAT_Y, FLOOR])
            cube([BAT_L, BAT_W, BAT_BAY_H]);

        // USB-C slot through front wall
        USB_CX = OUTER_W / 2;
        translate([USB_CX - USB_W / 2, -0.1, USB_Z_BOTTOM])
            cube([USB_W, WALL + 0.2, USB_H]);
        // Open channel from top of slot to top of body
        translate([USB_CX - USB_W / 2, -0.1, USB_Z_TOP])
            cube([USB_W, WALL + 0.2, BASE_DEPTH - USB_Z_TOP + 0.1]);

        // Battery wire channel
        translate([OUTER_W / 2 - 4, WALL + INNER_H / 2 - 3, FLOOR + BAT_BAY_H - 0.1])
            cube([8, 6, PCB_SHELF_Z - FLOOR - BAT_BAY_H + 0.2]);
    }

    // ---- PCB shelf brackets ----
    // Front wall: protrudes in +Y from inner front wall
    translate([WALL, WALL, PCB_SHELF_Z])
        shelf_x(INNER_W);

    // Back wall: protrudes in -Y from inner back wall
    // Mirror by placing at back wall and rotating 180° around Z
    translate([WALL + INNER_W, WALL + INNER_H, PCB_SHELF_Z])
        rotate([0, 0, 180])
            shelf_x(INNER_W);

    // Left wall: protrudes in +X from inner left wall
    translate([WALL, WALL, PCB_SHELF_Z])
        shelf_y(INNER_H);

    // Right wall: protrudes in -X from inner right wall
    translate([WALL + INNER_W, WALL + INNER_H, PCB_SHELF_Z])
        rotate([0, 0, 180])
            shelf_y(INNER_H);

    // Battery retaining walls
    BAT_X = WALL + (INNER_W - BAT_L) / 2;
    BAT_Y = WALL + (INNER_H - BAT_W) / 2;
    translate([BAT_X, BAT_Y + BAT_W, FLOOR + BAT_BAY_H])
        cube([BAT_L, 1.5, 2.0]);
    translate([BAT_X, BAT_Y - 1.5, FLOOR + BAT_BAY_H])
        cube([BAT_L, 1.5, 2.0]);
}

// =============================================================================
// LID
// =============================================================================
module lid() {
    USB_CX = OUTER_W / 2;

    difference() {
        union() {
            rounded_box(OUTER_W, OUTER_H, LID_T, CORNER_R);

            translate([WALL + TOLERANCE, WALL + TOLERANCE, LID_T])
                difference() {
                    cube([INNER_W - TOLERANCE * 2,
                          INNER_H - TOLERANCE * 2,
                          LIP_H]);
                    translate([LIP_T, LIP_T, -0.1])
                        cube([INNER_W - TOLERANCE * 2 - LIP_T * 2,
                              INNER_H - TOLERANCE * 2 - LIP_T * 2,
                              LIP_H + 0.2]);
                    // USB notch in BACK lip wall
                    translate([USB_CX - WALL - TOLERANCE - USB_W / 2,
                               INNER_H - TOLERANCE * 2 - LIP_T - 0.1, -0.1])
                        cube([USB_W, LIP_T + 0.2, LIP_H + 0.2]);
                }
        }

        // Screen window
        SW_X = WALL + PCB_PAD + SCREEN_LEFT;
        SW_Y = WALL + PCB_PAD + SCREEN_TOP;
        translate([SW_X, SW_Y, -0.1])
            cube([SCREEN_W, SCREEN_H, LID_T + 0.2]);

        // Chamfer around screen window
        CHAM = 0.8;
        translate([SW_X - CHAM, SW_Y - CHAM, LID_T - CHAM])
            cube([SCREEN_W + CHAM * 2, SCREEN_H + CHAM * 2, CHAM + 0.1]);
    }
}

// =============================================================================
// DETENT SYSTEM
// =============================================================================
// Small hemisphere bumps on the lid lip press into matching dimples in the
// base inner wall. One bump per long side, centered along the wall.
DETENT_R        = 0.5;   // bump radius
DETENT_INSET    = 1;   // how far up from base of lip the bump sits

module detent_bump() {
    sphere(r=DETENT_R);
}

module detent_dimple() {
    sphere(r=DETENT_R + 0.15);  // slightly larger for press-fit clearance
}

// =============================================================================
// LAYOUT
// =============================================================================
// --- BASE with dimples on inner LEFT and RIGHT (short) walls ---
difference() {
    base();
    // Dimple on inner left wall, centered on short axis, at lid lip height
    translate([WALL, WALL + INNER_H / 2, BASE_DEPTH - LIP_H / 2])
        detent_dimple();
    // Dimple on inner right wall
    translate([WALL + INNER_W, WALL + INNER_H / 2, BASE_DEPTH - LIP_H / 2])
        detent_dimple();
}

// --- LID with bumps on LEFT and RIGHT lip walls ---
translate([OUTER_W + 12, OUTER_H, LID_T + LIP_H])
    rotate([180, 0, 0])
        union() {
            lid();
            // Bump on left lip wall outer face, centered on short axis
            translate([WALL + TOLERANCE, WALL + TOLERANCE + (INNER_H - TOLERANCE * 2) / 2,
                       LID_T + DETENT_INSET])
                detent_bump();
            // Bump on right lip wall outer face
            translate([WALL + INNER_W - TOLERANCE,
                       WALL + TOLERANCE + (INNER_H - TOLERANCE * 2) / 2,
                       LID_T + DETENT_INSET])
                detent_bump();
        }
