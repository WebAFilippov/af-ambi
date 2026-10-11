// web.h - configuration and OTA web UI

#pragma once

// Starts the HTTP server on port 80 (station mode) or the setup portal
// (access-point mode).
void webBegin();

// Services HTTP requests and OTA uploads. Call from loop().
void webLoop();
