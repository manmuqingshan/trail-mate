# GPS jitter filtering (scheme combined with 6-axis IMU)

## Goal
When sampling team trajectories, filter out obviously unreasonable GPS jump points and reduce "large offset" records.
The solution is not to use the IMU to directly calculate the speed, but to use the IMU as a **physical feasibility constraint**.

## Feasibility conclusion
6-axis IMU **is not suitable for direct integration to obtain absolute speed** (drifts quickly), but is very suitable for:
- Determine the **stationary/moving** state
- Give the **acceleration upper limit** to limit the GPS speed change

Therefore, it is feasible and safe for the IMU to be used for **gating** (determining whether the GPS is physically reasonable).

## Input data
- GPS: `lat, lon, timestamp` (optional `speed/hdop`)
- IMU: acceleration + gyroscope (high frequency)

## Core logic
Every time the GPS fix comes, calculate the "velocity between these two points" and then use the IMU limit:

```
dt = t1 - t0
d  = distance(p0, p1)
v_gps = d / dt
```

### 1) IMU preprocessing (lightweight)
1. **Gravity removal**
 - Low-pass filtering to estimate gravity `g`
   - `a_lin = a_raw - g`
2. **Exercise intensity**
 - `a_rms` = Last 2–5 seconds `|a_lin|` RMS
 - `gyro_rms` = Last 2–5 seconds angular velocity RMS
3. **Motion status**
   - `is_stationary = (a_rms < A_STILL) && (gyro_rms < G_STILL)`

### 2) GPS Reasonableness determination
Define a conservative upper limit of speed `v_max`:

- If stationary:
 `v_max = V_STILL_MAX` (e.g. 0.5–1.0 m/s)
- If moving:
  `v_max = v_prev + a_max * dt + V_MARGIN`

Where:
- `v_prev` is the velocity of the last acceptance point
- `a_max` can come from the IMU Peak (re-limited) or fixed value
- `V_MARGIN` is the safety margin

Decision rules:
- If `v_gps > v_max` → **Consider drift, discard**
- Otherwise → **Accept**, and update `v_prev`

### 3) Failure protection
Avoid continuous misjudgments leading to "never update":
- Reject N times in succession → release once and set a low confidence mark
- Or gradually relax `V_MARGIN`

## Recommended initial parameters
Start conservatively (use the actual measurement log to adjust later):
- `A_STILL` = 0.05–0.08 g
- `G_STILL` = 2–4 deg/s
- `V_STILL_MAX` = 1.0 m/s
- `a_max` = 4.0 m/s^2 (walking/hiking), fast movement is available 8.0
- `V_MARGIN` = 0.5–1.0 m/s
- `N` = 3

## Why it works
Large GPS hop points mean extremely high "instantaneous speeds" that the IMU cannot possibly support while stationary or at low motion.
Using IMU as a constraint can reliably filter extreme drift without introducing the problem of integral drift.

## Limitations
- Absolute speed will not be calculated, only "feasibility judgment"
- Poor IMU installation and calibration will affect the threshold value
- When the GPS interval is very long, `dt` is large, and the threshold value must be more conservative

## Minimum implementation list
1. Make an IMU motion state estimate (RMS + static judgment)
2. Maintain `last_accepted_fix` and `v_prev`
3. Make a `v_gps` vs `v_max` judgment for each new GPS fix
4. Add log for parameter adjustment

## Recommended log format
```
[TRACK] gps dt=120s d=180m v=1.5m/s
[TRACK] imu a_rms=0.03g gyro=1.2dps state=STILL v_max=1.0
[TRACK] reject: v_gps>v_max
```
