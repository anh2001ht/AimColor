#pragma once
#include "ImprovedMouse.h"

class MouseRptParser : public HIDReportParser {
public:
 uint8_t prevButtons; // public so exec() can preserve real button state

 MouseRptParser() : prevButtons(0) {}

 void Parse(USBHID *hid, bool is_rpt_id, uint8_t len, uint8_t *buf) {
 // Report ID 1, minimum 7 bytes: [ID] [Buttons] [Xlo] [Xhi] [Ylo] [Yhi] [Wheel]
 if (buf[0] != 1 || len < 7) return;

 uint8_t buttons = buf[1];
 int16_t dx = (int16_t)((uint16_t)buf[3] << 8 | buf[2]);
 int16_t dy = (int16_t)((uint16_t)buf[5] << 8 | buf[4]);
 int8_t wheel = (int8_t)buf[6];

 // Only send a report if something actually changed.
 // Sends ONE USB HID report per incoming event - buttons + movement + wheel combined.
 // Fixes v1 bugs:
 // - Wheel was dropped when dx==0 && dy==0
 // - Button changes sent separate zero-movement reports before the movement report
 if ((buttons != prevButtons) || dx || dy || wheel) {
 Mouse.report(buttons, dx, dy, wheel);
 prevButtons = buttons;
 }
 }
};
