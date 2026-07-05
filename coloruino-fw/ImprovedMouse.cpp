#include "ImprovedMouse.h"

static const uint8_t _hidMultiReportDescriptorMouse[] PROGMEM = {
 0x05, 0x01, // Usage Page (Generic Desktop)
 0x09, 0x02, // Usage (Mouse)
 0xA1, 0x01, // Collection (Application)
 0x85, 0x01, // Report ID (1)
 0x09, 0x01, // Usage (Pointer)
 0xA1, 0x00, // Collection (Physical)
 0x05, 0x09, // Usage Page (Buttons)
 0x19, 0x01, // Usage Minimum (1)
 0x29, 0x05, // Usage Maximum (5)
 0x15, 0x00, // Logical Minimum (0)
 0x25, 0x01, // Logical Maximum (1)
 0x95, 0x05, // Report Count (5)
 0x75, 0x01, // Report Size (1)
 0x81, 0x02, // Input (Variable)
 0x95, 0x01, // Report Count (1)
 0x75, 0x03, // Report Size (3)
 0x81, 0x01, // Input (Constant) - padding
 0x05, 0x01, // Usage Page (Generic Desktop)
 0x09, 0x30, // Usage (X)
 0x09, 0x31, // Usage (Y)
 0x16, 0x01, 0x80, // Logical Minimum (-32767)
 0x26, 0xFF, 0x7F, // Logical Maximum (32767)
 0x75, 0x10, // Report Size (16)
 0x95, 0x02, // Report Count (2)
 0x81, 0x06, // Input (Variable, Relative)
 0x09, 0x38, // Usage (Wheel)
 0x15, 0x81, // Logical Minimum (-127)
 0x25, 0x7F, // Logical Maximum (127)
 0x75, 0x08, // Report Size (8)
 0x95, 0x01, // Report Count (1)
 0x81, 0x06, // Input (Variable, Relative)
 0xC0, // End Collection
 0xC0 // End Collection
};

Mouse_::Mouse_(void)
{
 static HIDSubDescriptor node(_hidMultiReportDescriptorMouse, sizeof(_hidMultiReportDescriptorMouse));
 HID().AppendDescriptor(&node);
}

void Mouse_::begin(void)
{
 end();
}

void Mouse_::end(void)
{
 _buttons = 0;
 move(0, 0, 0);
}

void Mouse_::click(uint8_t b)
{
 _buttons = b;
 move(0, 0, 0);
 _buttons = 0;
 move(0, 0, 0);
}

void Mouse_::move(int16_t x, int16_t y, int8_t wheel)
{
 HID_MouseReport_Data_t rpt;
 rpt.buttons = _buttons;
 rpt.xAxis = x;
 rpt.yAxis = y;
 rpt.wheel = wheel;
 SendReport(&rpt, sizeof(rpt));
}

void Mouse_::scroll(int8_t wheel)
{
 move(0, 0, wheel);
}

void Mouse_::buttons(uint8_t b)
{
 if (b != _buttons)
 {
 _buttons = b;
 move(0, 0, 0);
 }
}

void Mouse_::press(uint8_t b)
{
 buttons(_buttons | b);
}

void Mouse_::release(uint8_t b)
{
 buttons(_buttons & ~b);
}

void Mouse_::releaseAll(void)
{
 _buttons = 0;
 move(0, 0, 0);
}

bool Mouse_::isPressed(uint8_t b)
{
 if ((b & _buttons) > 0)
 return true;
 return false;
}

void Mouse_::report(uint8_t buttons, int16_t x, int16_t y, int8_t wheel)
{
 _buttons = buttons;
 HID_MouseReport_Data_t rpt;
 rpt.buttons = buttons;
 rpt.xAxis = x;
 rpt.yAxis = y;
 rpt.wheel = wheel;
 SendReport(&rpt, sizeof(rpt));
}

void Mouse_::SendReport(void *data, int length)
{
 HID().SendReport(HID_REPORTID_MOUSE, data, length);
}

Mouse_ Mouse;
