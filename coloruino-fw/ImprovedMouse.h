#pragma once

#define HID_REPORTID_NONE 0
#define HID_REPORTID_MOUSE 1

#if defined(ARDUINO_ARCH_AVR)
#define ATTRIBUTE_PACKED
#include "PluggableUSB.h"
#define EPTYPE_DESCRIPTOR_SIZE uint8_t
#else
#error "Unsupported architecture"
#endif

#include <HID.h>

#define MOUSE_LEFT (1 << 0)
#define MOUSE_RIGHT (1 << 1)
#define MOUSE_MIDDLE (1 << 2)
#define MOUSE_PREV (1 << 3)
#define MOUSE_NEXT (1 << 4)
#define MOUSE_ALL (MOUSE_LEFT | MOUSE_RIGHT | MOUSE_MIDDLE | MOUSE_PREV | MOUSE_NEXT)

typedef union ATTRIBUTE_PACKED
{
 uint8_t whole8[0];
 uint16_t whole16[0];
 uint32_t whole32[0];
 struct ATTRIBUTE_PACKED
 {
 uint8_t buttons;
 int16_t xAxis;
 int16_t yAxis;
 int8_t wheel;
 };
} HID_MouseReport_Data_t;

class Mouse_
{
public:
 Mouse_(void);
 void begin(void);
 void end(void);
 void click(uint8_t b = MOUSE_LEFT);
 void move(int16_t x, int16_t y, int8_t wheel = 0);
 void scroll(int8_t wheel);
 void press(uint8_t b = MOUSE_LEFT);
 void release(uint8_t b = MOUSE_LEFT);
 void releaseAll(void);
 bool isPressed(uint8_t b = MOUSE_LEFT);

 // Send a complete report in one shot: buttons + movement + wheel.
 // Does NOT send separate press/release reports - everything in one USB packet.
 void report(uint8_t buttons, int16_t x, int16_t y, int8_t wheel = 0);

 void SendReport(void *data, int length);

protected:
 uint8_t _buttons;
 inline void buttons(uint8_t b);
};
extern Mouse_ Mouse;
