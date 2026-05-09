// =============================================================================
// Literary Clock Case — Elecrow CrowPanel 4.2" ESP32-S3
// =============================================================================
// Board:    108mm × 87mm
// Screen:   86mm × 65mm
//           10mm from left, 6mm from top, 14.6mm from bottom
// Board stack: 9mm thick
// USB-C:    left short face, 14mm–31mm from bottom of board
// Battery:  3.7V LiPo, approx 72mm × 52mm × 8mm
//
// Two pieces: BASE (open top, battery bay, PCB shelf) + LID (screen window)
// Base prints open-side up. Lid prints face-down. No supports needed.
// =============================================================================

// ---- BOARD ----
PCB_W        = 110.0;   // long axis
PCB_H        = 89.0;    // short axis
PCB_T        = 9.0;     // total stack thickness

// ---- SCREEN ----
SCR_W        = 86.0;
SCR_H        = 65.0;
SCR_LEFT     = 11.0;    // from left edge of PCB
SCR_TOP      = 9.0;     // from top edge of PCB

// ---- USB slot (left short face, measured from bottom of board) ----
USB_BOT      = 14.0;
USB_TOP      = 31.0;
USB_H        = USB_TOP - USB_BOT;   // 17mm

// ---- BATTERY ----
BAT_L        = 72.0;
BAT_W        = 52.0;
BAT_T        = 8.0;

// ---- CASE ----
WALL         = 3.0;
FLOOR        = 2.5;
LID_T        = 2.5;     // lid face thickness
CORNER_R     = 5.0;
TOL          = 0.25;    // snap fit tolerance
LIP_H        = 4.0;     // lid lip height
LIP_T        = 1.5;     // lid lip wall thickness
PCB_PAD      = 1.0;     // gap around PCB in XY
BAT_PAD      = 2.0;     // padding above battery

// ---- PCB SHELF ----
SHELF_W      = 4.0;     // shelf protrusion (also = chamfer height for 45°)

// ---- BEVEL on screen window ----
BEVEL        = 3.0;     // bevel width on lid face around screen opening

// ---- DETENT ----
DET_R        = 1.3;

// ---- DERIVED ----
INNER_W      = PCB_W + PCB_PAD * 2;
INNER_H      = PCB_H + PCB_PAD * 2;
OUTER_W      = INNER_W + WALL * 2;
OUTER_H      = INNER_H + WALL * 2;

BAT_BAY_H    = BAT_T + BAT_PAD;
PCB_SHELF_Z  = FLOOR + BAT_BAY_H + 1.0;
BASE_DEPTH   = PCB_SHELF_Z + PCB_T;

// USB slot position in outer coords (board bottom = WALL + PCB_PAD)
USB_Y_BOT    = WALL + PCB_PAD + USB_BOT;
USB_Y_TOP    = WALL + PCB_PAD + USB_TOP;

$fn = 64;

// =============================================================================
// UTILITIES
// =============================================================================
module rounded_box(w, h, d, r) {
    hull() {
        for (x = [r, w-r]) for (y = [r, h-r])
            translate([x, y, 0]) cylinder(r=r, h=d);
    }
}

// Shelf bracket: hull between top ledge and bottom edge line.
// Top face at z=0 (caller places at PCB_SHELF_Z).
// 45° chamfer on underside — self-supporting.
module shelf_x(l) {
    hull() {
        cube([l, SHELF_W, 0.01]);
        translate([0, 0, -SHELF_W]) cube([l, 0.01, 0.01]);
    }
}
module shelf_y(l) {
    hull() {
        cube([SHELF_W, l, 0.01]);
        translate([0, 0, -SHELF_W]) cube([0.01, l, 0.01]);
    }
}

module det_bump()   { sphere(r=DET_R); }
module det_dimple() { sphere(r=DET_R + 0.15); }

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
        BX = WALL + (INNER_W - BAT_L) / 2;
        BY = WALL + (INNER_H - BAT_W) / 2;
        translate([BX, BY, FLOOR]) cube([BAT_L, BAT_W, BAT_BAY_H]);

        // USB slot — left short face, open toward top of body
        translate([-0.1, USB_Y_BOT, PCB_SHELF_Z])
            cube([WALL + 0.2, USB_H, PCB_T]);
        // Open channel from top of slot to open top of base
        translate([-0.1, USB_Y_BOT, PCB_SHELF_Z + PCB_T])
            cube([WALL + 0.2, USB_H, BASE_DEPTH]);

        // Battery wire channel
        translate([OUTER_W/2 - 5, WALL + INNER_H/2 - 3, FLOOR + BAT_BAY_H - 0.1])
            cube([10, 6, PCB_SHELF_Z - FLOOR - BAT_BAY_H + 0.2]);

        // Detent dimples on inner short walls (left and right)
        translate([WALL, WALL + INNER_H/2, PCB_SHELF_Z - LIP_H/2])
            det_dimple();
        translate([WALL + INNER_W, WALL + INNER_H/2, PCB_SHELF_Z - LIP_H/2])
            det_dimple();
    }

    // PCB shelf brackets — 4 sides, 45° chamfer underside
    translate([WALL, WALL, PCB_SHELF_Z])
        shelf_x(INNER_W);
    translate([WALL + INNER_W, WALL + INNER_H, PCB_SHELF_Z])
        rotate([0,0,180]) shelf_x(INNER_W);
    translate([WALL, WALL, PCB_SHELF_Z])
        shelf_y(INNER_H);
    translate([WALL + INNER_W, WALL + INNER_H, PCB_SHELF_Z])
        rotate([0,0,180]) shelf_y(INNER_H);

    // Battery retaining lips removed — battery held by recess fit
}

// =============================================================================
// LID
// Print face-down. Snap lip fits into base opening.
// Screen window has a bevelled edge.
// =============================================================================
module lid() {
    difference() {
        union() {
            // Face panel
            rounded_box(OUTER_W, OUTER_H, LID_T, CORNER_R);

            // Snap lip — fits inside base inner cavity
            translate([WALL + TOL, WALL + TOL, LID_T])
                difference() {
                    cube([INNER_W - TOL*2, INNER_H - TOL*2, LIP_H]);
                    // Hollow interior
                    translate([LIP_T, LIP_T, -0.1])
                        cube([INNER_W - TOL*2 - LIP_T*2,
                              INNER_H - TOL*2 - LIP_T*2,
                              LIP_H + 0.2]);
                    // USB notch in LEFT lip wall.
                    // rotate([180,0,0]) mirrors Y: assembled_y = OUTER_H - lid_y
                    // To match base USB at outer Y = USB_Y_BOT..USB_Y_TOP,
                    // lid outer Y must be OUTER_H-USB_Y_TOP .. OUTER_H-USB_Y_BOT
                    // In lip-local coords subtract (WALL+TOL):
                    translate([-0.1,
                               OUTER_H - USB_Y_TOP - WALL - TOL,
                               LIP_H - USB_H - 0.1])
                        cube([LIP_T + 0.2, USB_H, USB_H + 0.2]);
                }
        }

        // Screen window with bevel.
        // lid() is called BEFORE the rotate([180,0,0]) in layout.
        // After rotate, z=0 becomes the TOP (face) surface when assembled.
        // So in lid() coords: z=0 = face, z=LID_T = back.
        // Bevel: LARGE opening at z=0 (face/top), SMALL at z=LID_T (back).
        SW_X = WALL + PCB_PAD + SCR_LEFT;
        SW_Y = WALL + PCB_PAD + SCR_TOP;
        hull() {
            // Large opening at face (z=0)
            translate([SW_X - BEVEL, SW_Y - BEVEL, 0])
                cube([SCR_W + BEVEL*2, SCR_H + BEVEL*2, 0.01]);
            // Small opening at back (z=LID_T)
            translate([SW_X, SW_Y, LID_T - 0.01])
                cube([SCR_W, SCR_H, 0.01]);
        }
    }

    // Detent bumps on short lip walls (left and right)
    translate([WALL + TOL, WALL + TOL + (INNER_H - TOL*2)/2,
               LID_T + LIP_H/2])
        det_bump();
    translate([WALL + TOL + INNER_W - TOL*2 - LIP_T,
               WALL + TOL + (INNER_H - TOL*2)/2,
               LID_T + LIP_H/2])
        det_bump();
}

// =============================================================================
// LAYOUT
// =============================================================================
base();

translate([OUTER_W + 15, OUTER_H, LID_T + LIP_H])
    rotate([180, 0, 0])
        lid();

// =============================================================================
// NOTES
// =============================================================================
// BASE:  Open side up. Shelf brackets have 45° chamfer — no supports needed.
//        USB slot on left face, open toward top of body.
// LID:   Face down. Bevelled screen window. Snap lip fits inside base.
//        Detent bumps on short lip walls click into base dimples.
//
// FIT:   Lid too tight  → increase TOL (try 0.35)
//        Detent stiff   → decrease DET_R (try 1.1)
//        Shelf too small→ increase SHELF_W
//        Bevel too deep → decrease BEVEL
// =============================================================================
