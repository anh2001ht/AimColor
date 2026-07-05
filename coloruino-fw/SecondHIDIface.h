#pragma once
// ============================================================================
// SecondHIDIface.h
// Registers a second HID interface that exactly mirrors Interface 1 of the
// a real gaming mouse (VID=0x????, PID=0x????, spoofed).
//
// The real mouse presents:
// Interface 0 - HID Mouse (Boot, 16-bit axes, 5 buttons) ← ImprovedMouse
// Interface 1 - HID composite: Keyboard / Consumer / Vendor ← THIS FILE
//
// This interface is purely a descriptor stub - no data is ever sent on its
// endpoint. The host will poll EP2 IN and receive NAK until data is queued,
// which is normal for HID devices.
//
// Place this file in the sketch folder.
// In the .ino, add ONE global instance AFTER all other includes:
//
// #include "SecondHIDIface.h"
// SecondHIDIface_ SecondHIDIface; // global - registered before USB attach
// ============================================================================

#include "PluggableUSB.h"
#include "USBCore.h" // D_INTERFACE, D_ENDPOINT, USB_SendControl, etc.

// ── 140-byte HID report descriptor (verbatim from mouse_info.txt) ─────────
static const uint8_t _sHIDReportDesc[] PROGMEM = {
 // ── Keyboard (Report ID 3) ──────────────────────────────────────────────
 0x05, 0x01, // Usage Page (Generic Desktop)
 0x09, 0x06, // Usage (Keyboard)
 0xA1, 0x01, // Collection (Application)
 0x85, 0x03, // Report ID (3)
 0x05, 0x07, // Usage Page (Keyboard)
 0x19, 0xE0, // Usage Minimum (Left Ctrl)
 0x29, 0xE7, // Usage Maximum (Right GUI)
 0x15, 0x00, // Logical Minimum (0)
 0x25, 0x01, // Logical Maximum (1)
 0x95, 0x08, // Report Count (8)
 0x75, 0x01, // Report Size (1)
 0x81, 0x02, // Input (Variable)
 0x95, 0x06, // Report Count (6)
 0x75, 0x08, // Report Size (8)
 0x15, 0x00, // Logical Minimum (0)
 0x26, 0xFF, 0x00, // Logical Maximum (255)
 0x05, 0x07, // Usage Page (Keyboard)
 0x19, 0x00, // Usage Minimum (0)
 0x29, 0xE7, // Usage Maximum (Right GUI)
 0x81, 0x00, // Input ()
 0xC0, // End Collection

 // ── Consumer Control (Report ID 2) ─────────────────────────────────────
 0x05, 0x0C, // Usage Page (Consumer)
 0x09, 0x01, // Usage (Consumer Control)
 0xA1, 0x01, // Collection (Application)
 0x85, 0x02, // Report ID (2)
 0x19, 0x00, // Usage Minimum (0)
 0x2A, 0x9C, 0x02, // Usage Maximum (668)
 0x15, 0x00, // Logical Minimum (0)
 0x26, 0x9C, 0x02, // Logical Maximum (668)
 0x95, 0x01, // Report Count (1)
 0x75, 0x10, // Report Size (16)
 0x81, 0x00, // Input ()
 0xC0, // End Collection

 // ── Vendor Defined 0xFF00 (Report ID 6) ────────────────────────────────
 0x06, 0x00, 0xFF, // Usage Page (Vendor 0xFF00)
 0x09, 0x01, // Usage (0x01)
 0xA1, 0x01, // Collection (Application)
 0x85, 0x06, // Report ID (6)
 0x15, 0x00, // Logical Minimum (0)
 0x26, 0xFF, 0x00, // Logical Maximum (255)
 0x09, 0x2F, // Usage (0x2F)
 0x95, 0x02, // Report Count (2)
 0x75, 0x08, // Report Size (8)
 0x81, 0x02, // Input (Variable)
 0xC0, // End Collection

 // ── Vendor Defined 0xFF01 (Report ID 7, Feature) ───────────────────────
 0x06, 0x01, 0xFF, // Usage Page (Vendor 0xFF01)
 0x09, 0x01, // Usage (0x01)
 0xA1, 0x01, // Collection (Application)
 0x85, 0x07, // Report ID (7)
 0x15, 0x00, // Logical Minimum (0)
 0x26, 0xFF, 0x00, // Logical Maximum (255)
 0x09, 0x20, // Usage (0x20)
 0x95, 0x07, // Report Count (7)
 0x75, 0x08, // Report Size (8)
 0xB1, 0x02, // Feature (Variable)
 0xC0, // End Collection

 // ── System Control (Report ID 8) ───────────────────────────────────────
 0x05, 0x01, // Usage Page (Generic Desktop)
 0x09, 0x80, // Usage (System Control)
 0xA1, 0x01, // Collection (Application)
 0x85, 0x08, // Report ID (8)
 0x19, 0x81, // Usage Minimum (System Power Down)
 0x29, 0x83, // Usage Maximum (System Wake Up)
 0x15, 0x00, // Logical Minimum (0)
 0x25, 0x01, // Logical Maximum (1)
 0x75, 0x01, // Report Size (1)
 0x95, 0x03, // Report Count (3)
 0x81, 0x02, // Input (Variable)
 0x95, 0x05, // Report Count (5) - padding
 0x81, 0x01, // Input (Constant)
 0xC0 // End Collection
};

#define SECOND_HID_RPT_LEN ((uint16_t)sizeof(_sHIDReportDesc)) // must be 140

// ── Packed composite descriptor block sent by getInterface() ────────────────
// Layout mirrors what the real mouse sends for Interface 1:
// InterfaceDescriptor (9) + HIDDescriptor (9) + EndpointDescriptor (7) = 25 bytes
typedef struct __attribute__((packed)) {
 // Interface Descriptor
 uint8_t if_bLength; // 9
 uint8_t if_bDescriptorType; // 0x04
 uint8_t if_bInterfaceNumber; // assigned at plug time
 uint8_t if_bAlternateSetting; // 0
 uint8_t if_bNumEndpoints; // 1
 uint8_t if_bInterfaceClass; // 0x03 HID
 uint8_t if_bInterfaceSubClass; // 0x00 (no boot subclass)
 uint8_t if_bInterfaceProtocol; // 0x01 (keyboard protocol code - matches real mouse)
 uint8_t if_iInterface; // 0

 // HID Descriptor
 uint8_t hid_bLength; // 9
 uint8_t hid_bDescriptorType; // 0x21
 uint16_t hid_bcdHID; // 0x0110
 uint8_t hid_bCountryCode; // 0x00
 uint8_t hid_bNumDescriptors; // 1
 uint8_t hid_bClassDescType; // 0x22 = Report
 uint16_t hid_wDescriptorLength; // 140

 // Endpoint Descriptor
 uint8_t ep_bLength; // 7
 uint8_t ep_bDescriptorType; // 0x05
 uint8_t ep_bEndpointAddress; // 0x8N (IN | pluggedEndpoint)
 uint8_t ep_bmAttributes; // 0x03 Interrupt
 uint16_t ep_wMaxPacketSize; // 8
 uint8_t ep_bInterval; // 1 ms
} SecondHIDIfaceBlock;

// ============================================================================
class SecondHIDIface_ : public PluggableUSBModule {
 uint8_t _epType[1];

 // Called when the host requests the configuration descriptor.
 // Appends our interface + HID + endpoint block to the overall descriptor.
 int getInterface(uint8_t* interfaceCount) override {
 *interfaceCount += 1;
 SecondHIDIfaceBlock blk = {
 // Interface
 9, 0x04,
 pluggedInterface, // ← assigned by PluggableUSB at plug() time
 0, // alternate setting
 1, // one endpoint
 0x03, // HID
 0x00, // no boot subclass
 0x01, // keyboard protocol (matches real device)
 0,

 // HID class descriptor
 9, 0x21,
 0x0110, // HID 1.10
 0x00, // not localized
 1,
 0x22, // Report descriptor follows
 SECOND_HID_RPT_LEN,

 // Endpoint
 7, 0x05,
 (uint8_t)(pluggedEndpoint | 0x80), // IN direction
 0x03, // Interrupt transfer
 8, // wMaxPacketSize = 8 (matches real mouse)
 1 // 1 ms polling interval
 };
 return USB_SendControl(0, &blk, sizeof(blk));
 }

 // Called for GET_DESCRIPTOR requests routed through PluggableUSB.
 // We handle the HID Report Descriptor for our interface only.
 int getDescriptor(USBSetup& setup) override {
 // Standard GET_DESCRIPTOR, type = 0x22 (HID Report), index = our interface
 if (setup.bmRequestType == REQUEST_DEVICETOHOST_STANDARD_INTERFACE
 && setup.bRequest == GET_DESCRIPTOR
 && setup.wValueH == 0x22
 && setup.wIndex == pluggedInterface)
 {
 return USB_SendControl(TRANSFER_PGM, _sHIDReportDesc, SECOND_HID_RPT_LEN);
 }
 return 0; // not ours - let the chain continue
 }

 // Called for HID class requests (GET_REPORT, SET_REPORT, GET_IDLE, etc.)
 // We silently ACK anything directed at our interface; no data is sent.
 bool setup(USBSetup& setup) override {
 if (setup.wIndex != pluggedInterface) return false;
 uint8_t rt = setup.bmRequestType;
 if (rt == REQUEST_DEVICETOHOST_CLASS_INTERFACE
 || rt == REQUEST_HOSTTODEVICE_CLASS_INTERFACE)
 return true; // handled (ZLP reply is sent by the core)
 return false;
 }

 // No serial number for this interface.
 uint8_t getShortName(char* /*name*/) override { return 0; }

public:
 SecondHIDIface_() : PluggableUSBModule(1, 1, _epType) {
 _epType[0] = EP_TYPE_INTERRUPT_IN;
 PluggableUSB().plug(this); // registers before USB attach()
 }
};
// NOTE: Instantiate exactly ONCE at global scope in your .ino:
// SecondHIDIface_ SecondHIDIface;
