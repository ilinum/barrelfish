/*
 * Copyright (c) 2007-2013 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

/**/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <barrelfish/barrelfish.h>

#include "ehci_device.h"

#include <usb/usb.h>
#include <usb/usb_error.h>

#include "../../usb_controller.h"
#include "../../usb_device.h"
#include "usb_ehci.h"
#include "usb_ehci_memory.h"
#include "usb_ehci_bus.h"
#include "usb_ehci_root_hub.h"
#include "usb_ehci_xfer.h"

#define WAIT(ms) \
    for (int j = 0; j < ((ms)*10); j++) {printf("%c", 0xE);}

/* mackerel base */

/// the base for the EHCI control registers
static struct ehci_t ehci_base;

/**
 * \brief   performs a soft reset on the EHCI host controller
 */
static usb_error_t usb_ehci_hc_reset(usb_ehci_hc_t *hc)
{
    USB_DEBUG("resetting ehci controller.\n");
    /* write the reset bit */
    ehci_usbcmd_hcr_wrf(hc->ehci_base, 1);

    /* wait some time to let rest complete */
    WAIT(200);

    for (uint8_t i = 0; i < 10; i++) {
        if (ehci_usbcmd_hcr_rdf(hc->ehci_base)) {
            /*
             * the host controller sets this bit to 0 if the rest is complete
             * therefore we have to wait some more time
             */
            WAIT(200);
            continue;
        }
    }
    /* last check if reset was completed */
    if (ehci_usbcmd_hcr_rdf(hc->ehci_base)) {
        return (USB_ERR_TIMEOUT);
    }

    /*
     * checking if the reset was successfuly
     */
    if ((ehci_usbcmd_rawrd(hc->ehci_base) & 0xFFFFF0FF) != 0x00080000) {
        debug_printf("WARNING: Reset is succeeded: USBCMD has wrong value.\n");
    }
    if (ehci_usbsts_rawrd(hc->ehci_base) != 0x00001000) {
        debug_printf("WARNING: Reset is succeeded: USBST has wrong value.\n");
    }
    if (ehci_usbintr_rawrd(hc->ehci_base) != 0x00000000) {
        debug_printf("WARNING: Reset is succeeded: USBINTR has wrong value.\n");
    }
    if (ehci_frindex_rawrd(hc->ehci_base) != 0x00000000) {
        debug_printf("WARNING: Reset is succeeded: FRINDEX has wrong value.\n");
    }
    if (ehci_ctrldssegment_rawrd(hc->ehci_base) != 0x00000000) {
        debug_printf("WARNING: Reset is succeeded: CTRLDS has wrong value.\n");
    }
    if (ehci_configflag_rawrd(hc->ehci_base) != 0x00000000) {
        debug_printf(
                "WARNING: Reset is succeeded: CONFIGFLG has wrong value.\n");
    }
    if ((ehci_portsc_rawrd(hc->ehci_base, 0) | 0x00001000) != 0x00003000) {
        debug_printf("WARNING: Reset is succeeded: PORTSC has wrong value.\n");
    }

    return (USB_ERR_OK);
}

/**
 * \brief   halts the ehci controller, this lets the last transaction finish
 *          then stopts the processing of the HC
 */
static usb_error_t usb_ehci_hc_halt(usb_ehci_hc_t *hc)
{
    /* write the reset bit */
    ehci_usbcmd_wr(hc->ehci_base, 0x0);

    /*
     * Wait some time before start checking
     */
    WAIT(200);

    /* wait until the reset is done */
    for (uint8_t i = 0; i < 10; i++) {
        if (ehci_usbsts_hch_rdf(hc->ehci_base)) {
            /* all activity halted, return */
            return (USB_ERR_OK);
        }
        WAIT(200);
        continue;
    }
    /* check again if halted */
    if (ehci_usbsts_hch_rdf(hc->ehci_base)) {
        /* all activity halted, return */
        return (USB_ERR_OK);
    }
    return (USB_ERR_TIMEOUT);
}

static usb_error_t usb_ehci_initialize_controller(usb_ehci_hc_t *hc)
{

    USB_DEBUG(" initializing the controller hardware\n");
    if (ehci_hccparams_bit64ac_rdf(hc->ehci_base)) {
        debug_printf("NYI: Host controller uses 64-bit memory addresses!\n");
        return (USB_ERR_BAD_CONTEXT);

        ehci_ctrldssegment_wr(hc->ehci_base, 0);
    }

    /*
     * set the start of the lists
     */
    ehci_periodiclistbase_wr(hc->ehci_base, hc->pframes_phys);
    ehci_asynclistaddr_wr(hc->ehci_base, hc->qh_async_first->qh_self);

    /*
     * enable interrupts
     */
    ehci_usbintr_wr(hc->ehci_base, hc->enabled_interrupts);

    USB_DEBUG(" setting the command flags\n");
    /*
     * setting the start registers
     */
    ehci_usbcmd_t cmd = 0;
    // interrupt threshold to 1 micro frame
    cmd = ehci_usbcmd_itc_insert(cmd, 1);
    // enable the async schedule
    cmd = ehci_usbcmd_ase_insert(cmd, 1);
    // enable the periodic schedule
    cmd = ehci_usbcmd_pse_insert(cmd, 1);
    // keep the frame list size
    cmd = ehci_usbcmd_fls_insert(cmd, ehci_usbcmd_fls_rdf(hc->ehci_base));
    // start the host controller
    cmd = ehci_usbcmd_rs_insert(cmd, 1);

    ehci_usbcmd_wr(hc->ehci_base, cmd);

    USB_DEBUG(" start the host controller and take over port ownership\n");

    /*
     * take over port ownership
     */
    ehci_configflag_cf_wrf(hc->ehci_base, 1);

    /*
     * wait till the HC is up and running
     */
    WAIT(200);
    for (uint32_t i = 0; i < 10; i++) {
        if (!ehci_usbsts_hch_rdf(hc->ehci_base)) {
            break;
        }
        WAIT(200);
    }

    if (ehci_usbsts_hch_rdf(hc->ehci_base)) {
        /*
         * the hc does not want to do what we want
         */
        debug_printf("ERROR: Host controller does not start...\n");

        return (USB_ERR_IOERROR);
    }

    /*
     * R/WHost controller has port power control switches. This bit
     * represents the current setting of the switch (0 = off, 1 = on). When
     * power is not available on a port (i.e. PP equals a 0), the port is non-
     * functional and will not report attaches, detaches, etc.
     */
    if (ehci_hcsparams_ppc_rdf(hc->ehci_base)) {
        for (uint8_t i = 0; i < hc->root_hub_num_ports; i++) {
            ehci_portsc_pp_wrf(hc->ehci_base, i, 1);
        }
    }

    WAIT(300);
    debug_printf("EHCI controller up and running.\n");

    return (USB_ERR_OK);
}

/*
 * Exported Functions
 */

/**
 * \brief   Interrupt handler for the EHCI controller hardware
 *
 * \param   hc  host controller
 */
void usb_ehci_interrupt(usb_host_controller_t *host)
{
    usb_ehci_hc_t *hc = (usb_ehci_hc_t *)host->hc_control;

    USB_DEBUG("usb_ehci_interrupt()\n");

    /*
     * read the status register and mask out the interrupts [5..0]
     */
    ehci_usbsts_t intr = ehci_usbsts_rawrd(hc->ehci_base) & 0x3F;

    if (!(intr)) {
        /* there was no interrupt for this controller */
        return;
    }

    if (!(intr & hc->enabled_interrupts)) {
        /* there was an interrupt we dont have enabled */
        USB_DEBUG("Unenabled interrupt happened.\n");
        return;
    }

    /* acknowledge the interrupts */
    ehci_usbsts_wr(hc->ehci_base, intr);

    intr &= hc->enabled_interrupts;

    if (ehci_usbsts_hse_extract(intr)) {
        /*
         * host system error -> unrecoverable error
         * serious error occurs during a host system access involving the host
         * controller module.
         */
        debug_printf("EHCI controller encountered an unrecoverable error\n");
    }

    if (ehci_usbsts_pcd_extract(intr)) {
        /*
         * port change detected
         * there is something going on on the port e.g. a new device is attached
         *
         * This interrupt stays enabled untill the port is reset, so to avoid
         * multiple interrupts, disable the port change interrupts for now.
         */
        ehci_usbsts_pcd_insert(hc->enabled_interrupts, 0);
        ehci_usbsts_pcd_wrf(hc->ehci_base, 0);

        usb_ehci_roothub_interrupt(hc);
    }

    intr = ehci_usbsts_pcd_insert(intr, 0);
    intr = ehci_usbsts_iaa_insert(intr, 0);
    intr = ehci_usbsts_usbei_insert(intr, 0);
    intr = ehci_usbsts_usbi_insert(intr, 0);

    if (intr != 0) {
        /*
         * there is still an interrupt left, so block on this type
         */
        hc->enabled_interrupts &= ~intr;
        ehci_usbintr_wr(hc->ehci_base, hc->enabled_interrupts);
    }

    /*
     * poll the USB transfers
     */
    usb_ehci_poll(hc);
}

/**
 * \brief   initialize the host controller
 */
usb_error_t usb_ehci_init(usb_ehci_hc_t *hc, void *controller_base)
{
    printf("\n");
    USB_DEBUG("usb_ehci_init(%p)\n", controller_base);

    /* initialize the mackerel framework */
    hc->ehci_base = &ehci_base;
    ehci_initialize(hc->ehci_base, (mackerel_addr_t) controller_base, NULL);

    /* getting the operational register offset */
    uint8_t cap_offset = ehci_caplength_rd(hc->ehci_base);

    if (cap_offset == 0) {
        debug_printf("ERROR: EHCI capability register length is zero.\n");
        return (USB_ERR_INVAL);
    }

    ehci_initialize(hc->ehci_base, (mackerel_addr_t) controller_base,
            (mackerel_addr_t) (controller_base + cap_offset));

    USB_DEBUG(
            "EHCI mackerel device initialized.[cap base=%p, op-offset=%x]\n", controller_base, cap_offset);

    /*
     * read revision and number of ports
     */
    hc->revision = ehci_hciversion_rd(hc->ehci_base);
    hc->root_hub_num_ports = ehci_hcsparams_n_ports_rdf(hc->ehci_base);

    debug_printf("Device found: EHCI controller rev: %x.%x with %u ports\n",
            hc->revision >> 8, hc->revision & 0xFF, hc->root_hub_num_ports);

    usb_error_t err;
    err = usb_ehci_hc_halt(hc);
    if (err != USB_ERR_OK) {
        debug_printf("WARNING: Host controller has not halted properly.\n");
    }

    err = usb_ehci_hc_reset(hc);
    if (err != USB_ERR_OK) {
        debug_printf("ERROR: Host controller not reset properly. \n");
    }

    USB_DEBUG("Reset done.\n");

    if (ehci_usbcmd_fls_rdf(hc->ehci_base) == ehci_frame_rsvd) {
        /*
         * this field should be initialized to a correct values
         * having FLS=0x3 is invalid!
         */
        debug_printf("ERROR: Wrong frame length size\n");
        return (USB_ERR_IOERROR);
    }

    /* set some fields of the generic controller  */
    hc->controller->hcdi_bus_fn = usb_ehci_get_bus_fn();
    hc->controller->usb_revision = USB_REV_2_0;

    /* set the standard enabled interrupts */
    ehci_usbintr_t en_intrs = 0;
    en_intrs = ehci_usbintr_iaae_insert(en_intrs, 1);
    en_intrs = ehci_usbintr_hsee_insert(en_intrs, 1);
    en_intrs = ehci_usbintr_pcie_insert(en_intrs, 1);
    en_intrs = ehci_usbintr_usbie_insert(en_intrs, 1);
    en_intrs = ehci_usbintr_usbeie_insert(en_intrs, 1);
    hc->enabled_interrupts = en_intrs;

    usb_ehci_qh_t *qh;

    USB_DEBUG("ehci init: allocate QHs for interupt transfers\n");

    /* setup the terminate qheue head */
    qh = usb_ehci_qh_alloc();
    qh->qh_next_qtd = USB_EHCI_LINK_TERMINATE;
    qh->qh_alt_next_qtd = USB_EHCI_LINK_TERMINATE;
    qh->qh_status.status = USB_EHCI_QTD_STATUS_HALTED;
    hc->qh_terminate = qh;

    /*
     * initialize the queue heads for the interrupt transfer typ
     */
    for (uint32_t i = 0; i < USB_EHCI_VFRAMELIST_COUNT; i++) {
        qh = usb_ehci_qh_alloc();

        hc->qh_intr_last[i] = qh;
        qh->qh_ep.ep_speed = USB_EHCI_QH_SPEED_HIGH;
        qh->qh_ep.mult = 1;
        qh->qh_next_qtd = USB_EHCI_LINK_TERMINATE;
        qh->qh_alt_next_qtd = USB_EHCI_LINK_TERMINATE;
        qh->qh_status.status = USB_EHCI_QTD_STATUS_HALTED;
        qh->qh_self |= USB_EHCI_LINKTYPE_QH;
    }

    /*
     * build up the queue heads to be arranged in the 2ms polling interval
     */
    uint16_t bit = USB_EHCI_VFRAMELIST_COUNT / 2;
    uint32_t curr, next;

    while (bit) {
        curr = bit;
        while (curr & bit) {
            usb_ehci_qh_t *qh_curr;
            usb_ehci_qh_t *qh_next;

            next = (curr ^ bit) | (bit / 2);

            qh_curr = hc->qh_intr_last[curr];
            qh_next = hc->qh_intr_last[next];

            qh_curr->qh_link = qh_next->qh_self;

            curr++;
        }
        bit >>= 1;
    }

    qh = hc->qh_intr_last[0];
    qh->qh_link = USB_EHCI_LINK_TERMINATE;

    USB_DEBUG("ehci init: allocate iTDs and siTDs for isochronus transfers\n");
    /*
     * initialize the isochronus and isochronus split queues
     */
    for (uint32_t i = 0; i < USB_EHCI_VFRAMELIST_COUNT; i++) {

        /* allocate new transfer descriptors */
        usb_ehci_sitd_t *sitd = usb_ehci_sitd_alloc();
        hc->qh_sitd_fs_last[i] = sitd;

        sitd->sitd_self |= USB_EHCI_LINKTYPE_SITD;

        sitd->sitd_back_link = USB_EHCI_LINK_TERMINATE;

        next = i | (USB_EHCI_VFRAMELIST_COUNT / 2);
        sitd->sitd_next.address = hc->qh_intr_last[next]->qh_self;

        usb_ehci_itd_t *itd = usb_ehci_itd_alloc();
        hc->qh_itd_hs_last[i] = itd;

        itd->itd_self |= USB_EHCI_LINKTYPE_ITD;

        itd->itd_next.address = sitd->sitd_self;
    }

    USB_DEBUG("ehci init: initialize pframes\n");
    /*
     * initialize the pframes
     */
    if (hc->pframes == NULL) {
        usb_ehci_pframes_alloc(hc);
    }
    assert(hc->pframes);

    usb_paddr_t *pframes = (hc->pframes);

    for (uint32_t i = 0; i < USB_EHCI_FRAMELIST_COUNT; i++) {
        next = i & (USB_EHCI_VFRAMELIST_COUNT - 1);
        (pframes[i]) = hc->qh_itd_hs_last[next]->itd_self;
    }

    USB_DEBUG("ehci init: initialize async queue\n");
    /*
     * initialize the async queue
     */
    qh = usb_ehci_qh_alloc();

    hc->qh_async_last = qh;
    hc->qh_async_first = qh;
    qh->qh_self |= USB_EHCI_LINKTYPE_QH;

    qh->qh_ep.ep_speed = USB_EHCI_QH_SPEED_HIGH;
    qh->qh_ep.head_reclamation = 1;

    qh->qh_ep.mult = 1;
    qh->qh_link = qh->qh_self;
    qh->qh_alt_next_qtd = USB_EHCI_LINK_TERMINATE;
    qh->qh_next_qtd = USB_EHCI_LINK_TERMINATE;
    qh->qh_status.status = USB_EHCI_QTD_STATUS_HALTED;

    /* allocate the USB root hub device */
    struct usb_device *root_hub = usb_device_alloc(NULL, 0, 0, 1,
            USB_SPEED_HIGH, USB_MODE_HOST);

    if (root_hub) {
        hc->root_hub = root_hub;
        hc->controller->root_hub = root_hub;
    }

    hc->devices[USB_ROOTHUB_ADDRESS] = root_hub;

    // set the devices
    hc->controller->devices = hc->devices;

    err = usb_ehci_initialize_controller(hc);

    if (err == USB_ERR_OK) {
        usb_ehci_poll(hc);
    }

    return (err);
}