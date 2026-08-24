You are a senior embedded GUI engineer, please use LVGL 9.x (C/C++) to implement an SSTV receiving page (receiving end UI only).
Screen resolution: landscape 480(w) x 222(h).
TopBar height: 30px (must be strict).
Goal: Replicate the UI according to the following pixel layout; keep the whole thing as simple as possible; do not draw outer frames or add redundant container effects; maximize the picture display area; display status text + mode + audio level on the right; display a simple receiving progress bar (horizontal) at the bottom.

============================================================
1) Global color and style token (fixed, unified style)
============================================================
Main color (Amber): #EBA341 (0xEBA341)
Main Color Dark (AmberDark): #C98118
Background (WarmBG): #F6E6C6
Panel light bottom (PanelBG): #FAF0D8
Line: #E7C98F
Text: #6B4A1E
Weak text (TextDim): #8A6A3A
Success (Ok): #3E7D3E
Warn (Warn): #B94A2C
Gray (Gray): #6E6E6E

Font suggestion:
- TopBar title: Follow the system theme default font size
- Status text: 14px
- Substate/hint: 14px
- Small tag: 12~14px

Rounded corners: uniform 8px (also use 8px for picture boxes), no need for complex shadows/embossing.

============================================================
2) Overall page layout (pixel level)
============================================================
root: 480x222, background WarmBG.

Partition:
A) TopBar:x=0, y=0, w=480, h=30
B) Main:x=0, y=30, w=480, h=192

Main internal layout:
- Left: ImageArea (3:2 container) used to display received pictures
- Right: InfoArea (text information + audio level)
- Bottom: ProgressBar horizontal progress bar (located at the bottom of the right InfoArea, not covering the picture)

(1) Image container (strictly 3:2)
- img_box:x=8, y=0, w=288, h=192
 Note: img_box height must be 192 (Main full height), to meet 3:2, width = 192*1.5=288.
 This way the image on the left is maximized and proportioned correctly.
- img_box style: background PanelBG; border 2px Line; rounded corners 8px; no additional inner frame.
- Picture object img:
 - Place it inside the img_box, and the content is "equal-ratio centered and adapted":
 - If the received SSTV picture is 3:2, fill it directly;
 - If not, keep it equal to the ratio, leave it blank and use PanelBG.

(2) Right information area
- info_area:x=304, y=0, w=168, h=192
 (because 8px left margin + 288 image + 8px spacing => 304)
 Width 480 - 304 - 8 (right margin) = 168

(3) Progress bar position
- prog_bg: x=304, y=208, w=168, h=8 (absolute coordinates)
 (equivalent to y=178 in Main)

============================================================
3) TopBar (30px, simple)
============================================================
TopBar container: consistent with the existing UI of the system (reusing top_bar component)
Element:
- btn_back: x=8, y=4, w=22, h=22, display return icon, color TextDim
- title: Center-aligned, text "SSTV RECEIVER", color Text, font size 20
- battery: x=420, y=6 on the right displays the battery icon (placeholder) + text "100%" (x=446, y=5), color TextDim

============================================================
4) Right information area (info_area)
============================================================
Layout area: info_area, located on the right side of the picture

(1) Status/prompt text (label_state_sub)
- label_state_sub
 x=0, y=6, w=140 (relative to info_area)
 Text example:
    - "Listening for SSTV signal..."
    - "Decoding line: 120/240"
    - "Saved: /sstv/2026-02-09_001.bmp"
 Font: 14, color TextDim

(2) Parse metrics (added 3 lines)
Position: between label_state_sub and label_mode, about 80px height, 140px width
- label_metric_sync: x=0, y=34, w=140, text "SYNC: LOCK" / "SYNC: --"
- label_metric_slant: x=0, y=54, w=140, text "SLANT: --"
- label_metric_level: x=0, y=74, w=140, text "LEVEL: 42%"
Font: 14, color TextDim

(3) Mode display
- label_mode
  x=0, y=106, w=140
 Text example: "MODE: Auto" / "MODE: Scottie 1" / "MODE: PD240"
 Font: 14, color TextDim

(4) Ready/status prompt
- label_ready
  x=0, y=142, w=140
 Text example:
    - WAITING: "SSTV RX READY"(Text)
    - RECEIVING: "RECEIVING"(Ok + Text)
    - COMPLETE: "COMPLETE"(Ok)
    - ERROR: "ERROR"(Warn)
 Font: 14

============================================================
5) Audio level (vertical level meter on the right, required)
============================================================
Position: the far right side of info_area running through the upper middle area
- meter_box: x=136, y=34, w=32, h=120 (relative to info_area)

Style:
-vertical 12 segments, each segment is 8px high, and the segment spacing is 2px
- Bottom segment color Ok (green)
- Middle segment color #C18B2C (yellow brown)
- Top segment color Warn (orange)
- Unlit segment: Line or Gray (dark)
- Frame: 1~2px Line; background transparent or PanelBG

 Implementation:
- Composed of 12 small rectangles lv_obj
-Interface ui_sstv_set_audio_level(float level_0_1)
 - level_0_1 maps the number of lit segments n = round(level*12)
 - Lit n segments from the bottom

============================================================
6) Bottom progress bar (concise)
============================================================
Place it on the right InfoArea Bottom, without covering the image content:
- prog_bg: x=304, y=208, w=168, h=8 (absolute coordinates)
- Progress bar background: Line
- Progress bar filling: Amber
- Rounded corners: 2~4px
-Interface ui_sstv_set_progress(float p_0_1)

============================================================
7) State machine (supports at least WAITING / RECEIVING / COMPLETE / ERROR)
============================================================
WAITING:
- img_box display placeholder
- label_state_sub = "Listening for SSTV signal..."
- label_ready = "SSTV RX READY"(Text)
- progress = 0

RECEIVING:
- img refresh line by line
- label_state_sub = "Decoding line: x/xxx"
- label_ready = "RECEIVING"(Ok)
- progress follow the decoding progress

COMPLETE:
- img display the final image
- label_state_sub = "Image received" or "Saved: ..."
- label_ready = "COMPLETE"(Ok)
- progress = 1.0

ERROR:
- label_state_sub = error description
- label_ready = "ERROR"(Warn)

Required interface:
- void ui_sstv_set_state(enum State s)
- void ui_sstv_set_mode(const char* mode_str)
- void ui_sstv_set_audio_level(float level_0_1)
- void ui_sstv_set_progress(float p_0_1)
- void ui_sstv_set_image(const void* img_src_or_lv_img_dsc)

============================================================
8) Delivery Requirements
============================================================
 - Provide ui_sstv_create(lv_obj_t* parent) returns the root page object
 - Do not use any external PNG/icon resources (return arrows, battery life characters or simple vector drawing)
 - Strict 480x222, TopBar=30, img_box=288x192, 3:2
- The UI must be simple enough: no extra cards, no thick borders/shadows, no extra buttons (no Mode/Clear/Save)
- The code is compilable, no pseudocode

============================================================
9) Scottie mode (protocol description)
============================================================
VIS code:
- Scottie 1: 60 (decimal)
- Scottie 2: 56 (decimal)
- Scottie DX: 76 (decimal)

Color scanning order: green, blue, red (RGB)
Brightness frequency range: 1500–2300 Hz
Number of lines: 256 (Scottie 1/2)
Standard display: 320x256 (including 16 line headers)

Scottie 1 timing:
-Start sync (first line only): 9.0 ms @ 1200 Hz
- split/porch: 1.5 ms @ 1500 Hz
- Green scan: 138.240 ms
- split/porch: 1.5 ms @ 1500 Hz
- Blue scan: 138.240 ms
- Sync pulse: 9.0 ms @ 1200 Hz (between blue and red)
- Sync porch: 1.5 ms @ 1500 Hz
- Red scan: 138.240 ms

After the first line, repeat from "separation before green/porch" (no more starting sync).
Note: Scottie's sync is in the middle of the line (between blue and red), not at the beginning of the line.
Scottie 2 Timing Difference:
 - Green/Blue/Red Scan: 88.064 ms (0.2752 ms/pixel at 320 px)
Scottie DX Timing Difference:
 - Green/Blue/Red Scan: 345.6 ms (1.0800 ms/pixel at 320 px)
Other timing (sync/porch) is the same as Scottie 1.

============================================================
10) VIS header (JL Barber proposal)
============================================================
Source: "Proposal for SSTV Mode Specifications" (Dayton SSTV forum, 2000-05-20).
This description is used as a reference for VIS decoding alignment and parity checking.

VIS / calibration head sequence:
- 300 ms @ 1900 Hz(leader)
- 10 ms @ 1200 Hz(break)
- 300 ms @ 1900 Hz(leader)
- 30 ms @ 1200 Hz (VIS start bit)
- 7 data bits, LSB first, 30 ms per bit
  - 1100 Hz = "1"
  - 1300 Hz = "0"
- 30 ms Parity bits
 - Even = 1300 Hz
 - Odd = 1100 Hz
- 30 ms @ 1200 Hz (VIS stop bit)

Note: Mode timing begins immediately after the VIS stop bit.

============================================================
11) Robot 72 color (mode description)
============================================================
VIS code: 12 (decimal)
Color mode: Y, R-Y, B-Y
Scan order: Y, R-Y, B-Y
Number of lines: 240

Single line timing:
- Sync Pulse: 9.0 ms @ 1200 Hz
- Sync Porch: 3.0 ms @ 1500 Hz
- Y Scan: 138 ms
- Separate Pulse: 4.5 ms @ 1500 Hz
- Porch:1.5 ms @ 1900 Hz
- R-Y Sweep: 69 ms
 - Separate Pulse: 4.5 ms @ 2300 Hz
- Porch:1.5 ms @ 1500 Hz
- B-Y scan: 69 ms

Repeat the above sequence for a total of 240 lines.

============================================================
12) Robot 36 color (mode description)
============================================================
VIS code: 8 (decimal)
Color mode: Y, R-Y, B-Y
Scan order: Y, R-Y (even lines), Y, B-Y (odd lines)
Number of lines: 240

Two row example timing:
Even row:
- Sync Pulse: 9.0 ms @ 1200 Hz
- Sync Porch: 3.0 ms @ 1500 Hz
- Y scan: 88.0 ms
- "Even" separated pulses: 4.5 ms @ 1500 Hz
- Porch:1.5 ms @ 1900 Hz
- R-Y scan: 44.0 ms

Odd rows:
- Sync Pulse: 9.0 ms @ 1200 Hz
- Sync Porch: 3.0 ms @ 1500 Hz
- Y scan: 88.0 ms
- "Odd" separated pulses: 4.5 ms @ 2300 Hz
- Porch:1.5 ms @ 1900 Hz
- B-Y scan: 44.0 ms

Repeat the above sequence for a total of 240 lines.
Note: R-Y is sent on even lines and B-Y is sent on odd lines.

============================================================
13) Martin mode (mode description)
============================================================
VIS code:
-Martin 1:44 (decimal)
-Martin 2:40 (decimal)

Color mode: RGB (1500–2300 Hz)
Scan order: green, blue, red
Number of lines: 256

Color scanning time:
- Martin 1: 146.432 ms (0.4576 ms/pixel at 320 px)
- Martin 2: 73.216 ms (0.2288 ms/pixel at 320 px)

Timing per row:
- Sync pulse: 4.862 ms @ 1200 Hz
- Sync porch: 0.572 ms @ 1500 Hz
- Green scan
- Separated pulse: 0.572 ms @ 1500 Hz
- Blue scan
- Separated pulse: 0.572 ms @ 1500 Hz
- Red scan
- Separated pulse: 0.572 ms @ 1500 Hz

Repeat the above sequence for a total of 256 lines.

============================================================
14) PD mode (mode description)
============================================================
VIS code:
- PD50: 93 (decimal)
- PD90: 99 (decimal)
- PD120: 95 (decimal)
- PD160: 98 (decimal)
- PD180: 96 (decimal)
- PD240: 97 (decimal)
- PD290: 94 (decimal)

Color mode: Y, R-Y, B-Y
Scan order: Y (odd lines), R-Y (average of two lines), B-Y (average of two lines), Y (even lines)
Sync pulse: 20.0 ms @ 1200 Hz
Porch:2.080 ms @ 1500 Hz

Color scan time (Y, R-Y, B-Y):
- PD50:91.520 ms
- PD90:170.240 ms
- PD120:121.600 ms
- PD160:195.584 ms
- PD180:183.040 ms
- PD240:244.480 ms
- PD290:228.800 ms

Nominal resolution (all with 16 line headers):
- PD50:320x256
- PD90:320x256
- PD120:640x496
- PD160:512x400
- PD180:640x496
- PD240:640x496
- PD290:800x616

Note: This sink decodes at 320 pixels wide and scales vertically to the display height.

============================================================
15) Pasokon "P" mode (mode description)
============================================================
VIS code:
- P3: 113 (decimal)
- P5: 114 (decimal)
- P7: 115 (decimal)

Color mode: RGB (1500–2300 Hz)
Scan order: red, green, blue
Number of lines: 496 (including 16 line headers)

Color scanning time:
- P3:133.333 ms
- P5:200.000 ms
- P7:266.666 ms

Sync/porch duration:
- P3: Sync 5.208 ms, Porch 1.042 ms
- P5: Sync 7.813 ms, Porch 1.563 ms
- P7: Sync 10.417 ms, Porch 2.083 ms

Timing per row:
- sync pulse
- Porch
- Red scan
- Porch
- Green scan
- Porch
- Blue scan
- Porch

Repeat the above sequence for a total of 496 lines.
Note: This sink decodes at 320 pixels wide and scales vertically to the display height.

============================================================
16) Overview of PicoSSTV decoding algorithm
============================================================
Algorithm description reference:
- https://101-things.readthedocs.io/en/latest/sstv_decoder.html

Core implementation (PicoSSTV) is based on the following process:
1) Audio acquisition and preprocessing
 - ADC sampling at 15000 Hz
 - DC offset removal: dc = dc + (sample - dc) / 2
 - Use the DC-removed sample to enter decoding

2) SSB/IQ demodulation (decode_audio)
 - Fs/4 frequency shift with 4 phase shifts: audio -> (I,Q) rotation
 - Half-band filtering (half_band_filter2) low-pass demirroring
 - Rotate back to baseband again to get sample_i/sample_q

3) Frequency estimation and smoothing (decode_iq)
 - CORDIC phase calculation
 - frequency = last_phase - phase
 - frequency scaling: sample = (frequency * 15000) >> 16
 - IIR smoothing: smoothed = (smoothed*7 + sample) / 8
 - clamped to 1000..2500 Hz

4) Line synchronization detection + pattern recognition (decode)
 - Synchronization condition: frequency jumps from >=1300 to <1300
 - confirm Accumulate 10 sampling points below 1300 to determine valid hsync
 - Calculate line_length = sample_number - last_hsync_sample
 - Auto mode: +/-1% match with samples_per_line of each mode, choose the mode with the smallest error
 - Secondary confirmation: next sync Only enter decode_line if the threshold is still met
 - Note: Do not rely on VIS header, only rely on line synchronization length to identify the mode

5) Pixel mapping and output
 - sample_to_pixel maps image_sample to (x,y,colour)
 - Brightness: 1500..2300 Hz -> 0..255
 - Cumulative average of multiple samples of the same pixel
 - End image after end of line or y reaches max_height, return to detect_sync

6) Automatic slope correction (Auto Slant)
 - After each valid sync, update mean_samples_per_line with the actual line_length
 - Reduce the tilt of the entire image through IIR

7) Color and mode rendering
 - Martin/Scottie: RGB sequential mapping
 - Robot/PD: Y/Cr/Cb to RGB
 - BW: Grayscale direct mapping
