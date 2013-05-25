/*
 * Copyright (c) 2007-2013 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef LIBUSB_USB_H_
#define LIBUSB_USB_H_

#include <usb/usb_error.h>

/**
 *
 */
typedef enum usb_mode  {
    USB_MODE_HOST, /* initiates transfers */
    USB_MODE_DEVICE, /* bus transfer target */
    USB_MODE_DUAL /* can be host or device */
} usb_mode_t;
#define USB_MODE_MAX    (USB_MODE_DUAL+1)


/**
 * The USB device speed enumeration describes all possible USB speed
 * settings for a device.
 */
typedef enum usb_speed {
    USB_SPEED_VARIABLE,
    USB_SPEED_LOW,
    USB_SPEED_FULL,
    USB_SPEED_HIGH,
    USB_SPEED_SUPER,
} usb_speed_t;

typedef enum usb_hc_version {
    USB_UHCI=0x0100,
    USB_OHCI=0x0110,
    USB_EHCI=0x0200,
    USB_XHCI=0x0300
} usb_hc_version_t;

typedef enum usb_revision {
    USB_REV_UNKNOWN,
    USB_REV_PRE_1_0,
    USB_REV_1_0,
    USB_REV_1_1,
    USB_REV_2_0,
    USB_REV_2_5,
    USB_REV_3_0
} usb_revision_t;

/**
 *
 */
typedef enum usb_type {
    USB_TYPE_CTRL = 0,
    USB_TYPE_ISOC,
    USB_TYPE_BULK,
    USB_TYPE_INTR
} usb_type_t;


typedef enum usb_power {
    USB_POWER_MODE_OFF = 0,
    USB_POWER_MODE_ON = 1,
    USB_POWER_MODE_SAVE = 2,
    USB_POWER_MODE_SUSPEND = 3,
    USB_POWER_MODE_RESUME = 4
} usb_power_t;

/// the maximum power consumption in mA
#define USB_POWER_MAX 500

/// the minimum power requirements in mA
#define USB_POWER_MIN 100

/*
 * definition of the usb physical address type
 */
typedef volatile uintptr_t usb_paddr_t;

/**
 *
 */
struct usb_status {
    uint16_t wStatus;
};
typedef struct usb_status usb_status_t;

#define USB_STATUS_SELF_POWERED     0x0001;
#define USB_STATUS_REMOTE_WAKEUP    0x0002;
#define USB_STATUS_EP_HALT          0x0001;

/*
 * some delays
 */
#define USB_DELAY_PORT_RESET 10
#define USB_DELAY_PORT_ROOT_RESET 50
#define USB_DELAY_PORT_RECOVERY 10
#define USB_DELAY_PORT_POWERUP 100
#define USB_DELAY_PORT_RESUME 20
#define USB_DELAY_SET_ADDRESS 2
#define USB_DELAY_RESUME 100
#define USB_DELAY_WAIT 10
#define USB_DELAY_RECOVERY 10

#define USB_WAIT(ms) \
    for (uint32_t i = 0; i < 0xFFF*ms; i++) {};

#define USB_DEBUG(x...) debug_printf(x)

usb_error_t usb_lib_init(void);

#endif