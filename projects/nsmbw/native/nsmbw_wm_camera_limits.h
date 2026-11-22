#pragma once
// World-map camera limits for 21:9 (nsmbw_wm_camera_clamp.cpp, nsmbw_tick_read_pump.cpp).
// The target x the camera may use on the current map; returns x unchanged when off.
double NsmbwWmClampCameraX(double x);
// The extra half-width to pull each limit in by (0 when the view is not wider than 16:9).
float NsmbwWmCameraMargin();
// The target z, clamped to the map's free-look depth range (unchanged when off or not measured).
double NsmbwWmClampCameraZ(double z);
