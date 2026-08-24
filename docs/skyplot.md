You are a senior embedded GUI engineer. Please use LVGL 8.x (C/C++) to implement a "GNSS Sky Plot (Satellite Sky Map)" page.
Screen resolution: landscape 480(w) × 222(h).
Requirements: Draw strictly according to the pixel layout given below; do not change the layout on your own; do not change element copy; the color system must revolve around the main color 0xEBA341 and be consistent with the warm color engineering style; all controls must be reusable and data refreshable.

============================================================
1) Overall page layout (pixel level)
============================================================
- Page root container root: size 480×222, background color is light warm beige (see style token).
- Divided into three parts:
 A. Top TopBar: height 30px, full width 480
 B. Content area Content: height 222-30=192px, y=30
 - left SkyPlotPanel: x=8, y=38, w=277, h=176
 - right StatusPanel: x=293, y=38, w=179, h=176
 Note: Leave 8px spacing at the top of the Content area, so y=30+8=38 for the two panels; leave 8px spacing between the left and right panels.

- Global rounded corners: 8px
- Global stroke: 2px (main color emphasizes the edge)

============================================================
2) Style and color token (must be used, unified style)
============================================================
Main color (Amber): #EBA341 (0xEBA341)
Main color dark (AmberDark): #C98118
Background (WarmBG): #F6E6C6
Panel bottom (PanelBG): #FAF0D8
Line: #E7C98F
Text: #6B4A1E
TextDim: #8A6A3A
Warn: #B94A2C
Success (Ok): #3E7D3E
Information blue (Info): #2D6FB6

Constellation color (for SYS/legend):
GPS:  #E3B11F
GLN:  #2D6FB6
GAL:  #3E7D3E
BD:   #B94A2C

SNR status color (for outer ring/padding of points):
SNR_GOOD:#3E7D3E
SNR_FAIR:#8FBF4D
SNR_WEAK:#C18B2C
NOT_USED:#B94A2C
IN_VIEW: #6E6E6E (gray)

Font:
- Title/column header: 20px (or LVGL Built-in approximate font size)
- Header: 14px
- List content: 16px
- Bottom summary: 14px

============================================================
3) TopBar (height 30)
============================================================
TopBar container: x=0,y=0,w=480,h=30, background WarmBG, bottom divider 2px Line.
Left title:
- label_title: changed to Summary text: "USE: 7/18|HDOP: 1.6|FIX: 3D"
- The three sections of USE/HDOP/FIX use different colors, and the whole is coordinated with the theme

Battery display on the right (only placeholder, does not implement the battery algorithm):
- battery_icon: x=410, y=6, w=26, h=14, stroke 2px TextDim, filled with transparent, small bump on the right 3×6.
- label_batt: x=442, y=5, text "100%", color TextDim, font size 18.

============================================================
4) Left SkyPlotPanel (x=8,y=38,w=292,h=176)
============================================================
Panel container:
- panel_sky: Rounded Corners 10, Background PanelBG, Stroke 2px AmberDark.

Draw a "sky circle chart" internally:
- Circle chart circumscribed square area sky_area: x=10, y=2, w=170, h=170 (relative to panel_sky)
 => Circle center C = (10+85, 2+85) = (95,87) relative to panel_sky Internal coordinates
 => Radius R = 82 (leaving space for stroke and text)

Drawing content (you can use lv_canvas or self-drawn objects, but they must be rendered):
a) Outer circle (horizon 0°): Ring stroke Line 2px
b) 3 concentric circles (30°/60°/90°):
 - r=R*2/3 (about 55)
 - r=R*1/3 (about 27)
 - r=0 (center point)
 Use Line 1px dotted line effect for the circular line (if you cannot do a dotted line, you can also use a thin solid line)
c) Cross bearing lines: N-S and E-W pass through the center of the circle, line color Line 1px
d) Direction text:
 - "N" placed above the circle: the center is aligned with the center x, y=2-2 (close to the outside of the circle)
 - "E" Place on the right side: x=10+170+8,y=95-10
 - "W" Place on the left side: x=2,y=95-10 (in the blank space on the left side of the circle)
 Text color, font size 18
e) Elevation angle scale text:
 - Change the scale to "pointing to the 10:30 direction": mark "90°" (near the outer circle), "60°" (r=55), and "30°" (r=27) on the diagonal line from the center of the circle to the 10:30 direction.
 - The text is arranged along the diagonal line, and the overall visual connection points to the 10 and a half direction
 Text color TextDim, font size 16
f) Marking near the center of the circle:
 - label_horizon: placed slightly below the center of the circle: text "0° Horizon", color TextDim, font size 12

Satellite point drawing (dynamic):
-Use a small dot object sat_dot for each satellite (recommended lv_obj + fillet = radius)
-Point radius: 10px (diameter 20)
-Display satellites within the point ID (two or three digits), text white or dark brown (contrast is automatically selected according to the background color, white is preferred)
-The position of the point is mapped to the circle by (azimuth, elevation):
   r = R * (1 - elevation/90)
   x = Cx + r * sin(azimuth)
   y = Cy - r * cos(azimuth)
  (azimuth: 0°=N, 90°=E)
-Point fill color: by constellation (GPS/GLN/GAL/BD)
- Click outer ring/small mark: press SNR status (GOOD/FAIR/WEAK/NOT_USED/IN_VIEW):
 - GOOD/FAIR/WEAK: use the corresponding color to make a 2px outer ring
 - NOT_USED: outer ring Warn
 - IN_VIEW: outer ring Gray
- If the satellite used_in_fix=true, add a small label "USE" next to the point:
 - use_tag: Rounded corners 6, background Ok, text white, font size 12
 - Position: Offset the lower left corner of the pasting point (dx=-12, dy=12) to avoid blocking the numbers

Legend (in the lower right corner area of ​​SkyPlotPanel):
- legend_sys (constellation legend) is placed on the right side of the circle chart, below:
 - Small color block 10×10 + Text (GPS/GLONASS/Galileo/BeiDou)
 - Position: starting from x=190, y=105, line height 15
 - legend_snr (SNR legend) is placed above legend_sys and aligned to its left (upper right corner area of SkyPlotPanel):
 - Text order: "SNR Good" "SNR Weak" "Not Used" "In View"
 - Use small dots (diameter 10) + text
 - Position: starting from x=190, arranged vertically (line height 15), the whole is above legend_sys; the distance from legend_sys is 6px; the whole is moved up 30px; small dots and text arrangement rules are the same as legend_sys (color block y+4, text x+14)
 - text color TextDim, font size 12

============================================================
5) Right StatusPanel (x=293, y=38, w=179, h=176)
============================================================
Panel container:
- panel_status: rounded corners 10, background PanelBG, stroke 2px AmberDark

Title bar (height 26):
- header: x=0,y=0,w=179,h=26, background Amber, upper half of rounded corners maintained
- label: centered "SATELLITE STATUS", text color #2A1A05, font size 14 bold

Header row (height 22, y=26):
- Background: #F2D9A5
- Columns: ID / SYS / ELEV / SNR / USE
- Column width (pixels): ID=24, SYS=38, ELEV=39, SNR=38, USED=39 (ELEV=USED, SNR=SYS)
- Text centered, color TextDim, font size 12

Data list area (y=48 to y=176, height 128):
-Rendered in "fixed row height" mode, row height 17px, 7 rows can be displayed (remaining scrolling/pagination is not implemented, but the structure must be reserved)
-5 columns in each row to align the header
- Line bottom divider 1px Line
- SYS text color according to constellation color (GPS/GLN/GAL/BD)
- USED column: if used=true, display "YES" color Ok; otherwise "NO" color Warn
- Other column text colors Text
- When the number of satellites exceeds the number of rows that can be displayed, the list is displayed in the following priority order: USED=YES first, high SNR first, then high elevation, then PRN/SVID in ascending order

The summary area at the bottom is removed, and all space is given to the data list area

============================================================
6) Data structure and refresh interface (must be provided)
============================================================
Define data structure (example):
- struct SatInfo {
    int id;           // PRN/SVID
    enum Sys {GPS, GLN, GAL, BD} sys;
    float azimuth;    // 0..359 deg
    float elevation;  // 0..90 deg
    int snr;          // dB-Hz
    bool used;
    enum SNRState {GOOD, FAIR, WEAK, NOT_USED, IN_VIEW} snr_state;
  };

- struct GnssStatus {
    int sats_in_use;
    int sats_in_view;
    float hdop;
    enum Fix {NOFIX, FIX2D, FIX3D} fix;
  };

Must implement two refresh functions (for upper layer calls):
- void ui_gnss_skyplot_set_sats(const SatInfo* sats, int count);
 -> Update circular chart satellite points (create/reuse objects), update the first N rows of the right table
- void ui_gnss_skyplot_set_status(GnssStatus st);
 -> Update bottom summary text and FIX Color

============================================================
7) Interaction (minimal)
============================================================
- The Back button position is allowed to be reserved in the upper left corner (but this requirement does not have to be implemented).
- The page does not require touch interaction; only the display needs to be refreshed.
- Backspace key behavior: trigger the Back button logic of TopBar, consistent with other pages.

============================================================
8) Deliverable requirements
============================================================
- Provide a function ui_gnss_skyplot_create(lv_obj_t* parent) to return the page root object.
- All object pointers are stored in static/structural bodies, supporting repeated page entry without leakage.
- Do not use external image resources; draw everything with LVGL.
 - Output code should be directly compilable (pseudocode is not accepted).

Generate complete LVGL page code according to the above requirements.
