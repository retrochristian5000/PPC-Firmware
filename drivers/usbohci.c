/*
 * Driver for USB OHCI ported from CoreBoot
 *
 * Copyright (C) 2014 BALATON Zoltan
 *
 * This file was part of the libpayload project.
 *
 * Copyright (C) 2010 Patrick Georgi
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

//#define USB_DEBUG_ED

#include "config.h"
#include <asm/io.h>
#include <libopenbios/ofmem.h>
#include "timer.h"
#include "drivers/pci.h"
#include "pci.h"
#include <drivers/usb.h>
#include "usbohci_private.h"
#include "usbohci.h"

static void ohci_start (hci_t *controller);
static void ohci_stop (hci_t *controller);
static void ohci_reset (hci_t *controller);
static void ohci_shutdown (hci_t *controller);
static int ohci_bulk (endpoint_t *ep, int size, u8 *data, int finalize);
static int ohci_control (usbdev_t *dev, direction_t dir, int drlen, void *devreq,
			 int dalen, u8 *data);
static void* ohci_create_intr_queue (endpoint_t *ep, int reqsize, int reqcount, int reqtiming);
static void ohci_destroy_intr_queue (endpoint_t *ep, void *queue);
static u8* ohci_poll_intr_queue (void *queue);
static void ohci_process_done_queue(ohci_t *ohci, int spew_debug);

static void *
ohci_alloc_aligned(size_t alignment, size_t size)
{
	void *ptr = NULL;

	if (ofmem_posix_memalign(&ptr, alignment, size) != 0)
		return NULL;
	return ptr;
}

#ifdef USB_DEBUG_ED
static void
dump_td (td_t *cur)
{
	u32 direction = (__le32_to_cpu(cur->config) & TD_DIRECTION_MASK) >>
		TD_DIRECTION_SHIFT;

	usb_debug("+---------------------------------------------------+\n");
	if (direction == OHCI_SETUP)
		usb_debug("|..[SETUP]..........................................|\n");
	else if (direction == OHCI_IN)
		usb_debug("|..[IN].............................................|\n");
	else if (direction == OHCI_OUT)
		usb_debug("|..[OUT]............................................|\n");
	else
		usb_debug("|..[]...............................................|\n");
	usb_debug("|:|============ OHCI TD at [0x%08lx] ==========|:|\n", virt_to_phys(cur));
	usb_debug("|:| ERRORS = [%ld] | CONFIG = [0x%08x] |        |:|\n",
		  3 - ((__le32_to_cpu(cur->config) & (3UL << 26)) >> 26), __le32_to_cpu(cur->config));
	usb_debug("|:+-----------------------------------------------+:|\n");
	usb_debug("|:|   C   | Condition Code               |   [%02ld] |:|\n",
		 (__le32_to_cpu(cur->config) & (0xFUL << 28)) >> 28);
	usb_debug("|:|   O   | Direction/PID                |    [%ld] |:|\n",
		 (__le32_to_cpu(cur->config) & TD_DIRECTION_MASK) >> TD_DIRECTION_SHIFT);
	usb_debug("|:|   N   | Buffer Rounding              |    [%ld] |:|\n",
		 (__le32_to_cpu(cur->config) & (1UL << 18)) >> 18);
	usb_debug("|:|   F   | Delay Interrupt              |    [%ld] |:|\n",
		 (__le32_to_cpu(cur->config) & (7UL << 21)) >> 21);
	usb_debug("|:|   I   | Data Toggle                  |    [%ld] |:|\n",
		 (__le32_to_cpu(cur->config) & (3UL << 24)) >> 24);
	usb_debug("|:|   G   | Error Count                  |    [%ld] |:|\n",
		 (__le32_to_cpu(cur->config) & (3UL << 26)) >> 26);
	usb_debug("|:+-----------------------------------------------+:|\n");
	usb_debug("|:| Current Buffer Pointer         [0x%08x]   |:|\n", __le32_to_cpu(cur->current_buffer_pointer));
	usb_debug("|:+-----------------------------------------------+:|\n");
	usb_debug("|:| Next TD                        [0x%08x]   |:|\n", __le32_to_cpu(cur->next_td));
	usb_debug("|:+-----------------------------------------------+:|\n");
	usb_debug("|:| Current Buffer End             [0x%08x]   |:|\n", __le32_to_cpu(cur->buffer_end));
	usb_debug("|:|-----------------------------------------------|:|\n");
	usb_debug("|...................................................|\n");
	usb_debug("+---------------------------------------------------+\n");
}

static void
dump_ed (ed_t *cur)
{
	td_t *tmp_td = NULL;
	usb_debug("+===================================================+\n");
	usb_debug("| ############# OHCI ED at [0x%08lx] ########### |\n", virt_to_phys(cur));
	usb_debug("+---------------------------------------------------+\n");
	usb_debug("| Next Endpoint Descriptor       [0x%08lx]       |\n", __le32_to_cpu(cur->next_ed) & ~0xFUL);
	usb_debug("+---------------------------------------------------+\n");
	usb_debug("|        |               @ 0x%08x :             |\n", __le32_to_cpu(cur->config));
	usb_debug("|   C    | Maximum Packet Length           | [%04ld] |\n",
		 ((__le32_to_cpu(cur->config) & (0x3fffUL << 16)) >> 16));
	usb_debug("|   O    | Function Address                | [%04d] |\n",
		 __le32_to_cpu(cur->config) & 0x7F);
	usb_debug("|   N    | Endpoint Number                 |   [%02ld] |\n",
		 (__le32_to_cpu(cur->config) & (0xFUL << 7)) >> 7);
	usb_debug("|   F    | Endpoint Direction              |    [%ld] |\n",
		 ((__le32_to_cpu(cur->config) & (3UL << 11)) >> 11));
	usb_debug("|   I    | Endpoint Speed                  |    [%ld] |\n",
		 ((__le32_to_cpu(cur->config) & (1UL << 13)) >> 13));
	usb_debug("|   G    | Skip                            |    [%ld] |\n",
		 ((__le32_to_cpu(cur->config) & (1UL << 14)) >> 14));
	usb_debug("|        | Format                          |    [%ld] |\n",
		 ((__le32_to_cpu(cur->config) & (1UL << 15)) >> 15));
	usb_debug("+---------------------------------------------------+\n");
	usb_debug("| TD Queue Tail Pointer          [0x%08lx]       |\n",
		 __le32_to_cpu(cur->tail_pointer) & ~0xFUL);
	usb_debug("+---------------------------------------------------+\n");
	usb_debug("| TD Queue Head Pointer          [0x%08lx]       |\n",
		 __le32_to_cpu(cur->head_pointer) & ~0xFUL);
	usb_debug("| CarryToggleBit    [%d]          Halted   [%d]       |\n",
		 (u16)(__le32_to_cpu(cur->head_pointer) & 0x2UL)>>1, (u16)(__le32_to_cpu(cur->head_pointer) & 0x1UL));

	tmp_td = (td_t *)phys_to_virt((__le32_to_cpu(cur->head_pointer) & ~0xFUL));
	if ((__le32_to_cpu(cur->head_pointer) & ~0xFUL) != (__le32_to_cpu(cur->tail_pointer) & ~0xFUL)) {
		usb_debug("|:::::::::::::::::: OHCI TD CHAIN ::::::::::::::::::|\n");
		while (virt_to_phys(tmp_td) != (__le32_to_cpu(cur->tail_pointer) & ~0xFUL))
		{
			dump_td(tmp_td);
			tmp_td = (td_t *)phys_to_virt((__le32_to_cpu(tmp_td->next_td) & ~0xFUL));
		}
		usb_debug("|:::::::::::::::: EOF OHCI TD CHAIN ::::::::::::::::|\n");
		usb_debug("+---------------------------------------------------+\n");
	} else {
		usb_debug("+---------------------------------------------------+\n");
	}
}
#endif

static void
ohci_reset (hci_t *controller)
{
	if (controller == NULL)
		return;

	OHCI_INST(controller)->opreg->HcCommandStatus = __cpu_to_le32(HostControllerReset);
	mdelay(2); /* wait 2ms */
	OHCI_INST(controller)->opreg->HcControl = 0;
	mdelay(10); /* wait 10ms */
}

static void
ohci_reinit (__attribute__((unused)) hci_t *controller)
{
}

hci_t *
ohci_init (void *bar)
{
	int i;
	hci_t *controller = new_controller ();
	ed_t *periodic_ed = NULL;

	if (!controller) {
		printk("Could not create USB controller instance.\n");
		return NULL;
	}

	controller->instance = malloc (sizeof (ohci_t));
	if(!controller->instance) {
		printk("Not enough memory creating USB controller instance.\n");
		goto fail_controller;
	}
	memset(controller->instance, 0, sizeof(ohci_t));

	controller->type = OHCI;

	controller->start = ohci_start;
	controller->stop = ohci_stop;
	controller->reset = ohci_reset;
	controller->init = ohci_reinit;
	controller->shutdown = ohci_shutdown;
	controller->bulk = ohci_bulk;
	controller->control = ohci_control;
	controller->set_address = generic_set_address;
	controller->finish_device_config = NULL;
	controller->destroy_device = NULL;
	controller->create_intr_queue = ohci_create_intr_queue;
	controller->destroy_intr_queue = ohci_destroy_intr_queue;
	controller->poll_intr_queue = ohci_poll_intr_queue;
	for (i = 0; i < 128; i++) {
		controller->devices[i] = 0;
	}
	init_device_entry (controller, 0);
	if (!controller->devices[0]) {
		printk("Not enough memory creating OHCI root hub.\n");
		goto fail_instance;
	}
	OHCI_INST (controller)->roothub = controller->devices[0];

	controller->reg_base = (u32)(unsigned long)bar;
	OHCI_INST (controller)->opreg = (opreg_t*)phys_to_virt(controller->reg_base);
	usb_debug("OHCI Version %x.%x\n",
		  (READ_OPREG(OHCI_INST(controller), HcRevision) >> 4) & 0xf,
		  READ_OPREG(OHCI_INST(controller), HcRevision) & 0xf);

	if ((READ_OPREG(OHCI_INST(controller), HcControl) & HostControllerFunctionalStateMask) == USBReset) {
		/* cold boot */
		OHCI_INST (controller)->opreg->HcControl &= __cpu_to_le32(~RemoteWakeupConnected);
		OHCI_INST (controller)->opreg->HcFmInterval =
			__cpu_to_le32((11999 * FrameInterval) | ((((11999 - 210)*6)/7) * FSLargestDataPacket));
		/* TODO: right value for PowerOnToPowerGoodTime ? */
		OHCI_INST (controller)->opreg->HcRhDescriptorA =
			__cpu_to_le32(NoPowerSwitching | NoOverCurrentProtection | (10 * PowerOnToPowerGoodTime));
		OHCI_INST (controller)->opreg->HcRhDescriptorB = __cpu_to_le32(0 * DeviceRemovable);
		udelay(100); /* TODO: reset asserting according to USB spec */
	} else if ((READ_OPREG(OHCI_INST(controller), HcControl) & HostControllerFunctionalStateMask) != USBOperational) {
		OHCI_INST (controller)->opreg->HcControl =
			__cpu_to_le32((READ_OPREG(OHCI_INST(controller), HcControl) & ~HostControllerFunctionalStateMask)
			| USBResume);
		udelay(100); /* TODO: resume time according to USB spec */
	}
	int interval = OHCI_INST (controller)->opreg->HcFmInterval;

	OHCI_INST (controller)->opreg->HcCommandStatus = __cpu_to_le32(HostControllerReset);
	udelay (10); /* at most 10us for reset to complete. State must be set to Operational within 2ms (5.1.1.4) */
	OHCI_INST (controller)->opreg->HcFmInterval = interval;
	OHCI_INST (controller)->hcca = ohci_alloc_aligned(256, 256);
	if (!OHCI_INST (controller)->hcca) {
		printk("Not enough memory creating OHCI HCCA.\n");
		goto fail_root;
	}
	memset((void*)OHCI_INST (controller)->hcca, 0, 256);

	usb_debug("HCCA addr %p\n", OHCI_INST(controller)->hcca);
	/* Initialize interrupt table. */
	ohci_t *const ohci = OHCI_INST(controller);
	periodic_ed = ohci_alloc_aligned(sizeof(ed_t), sizeof(ed_t));
	if (!periodic_ed) {
		printk("Not enough memory creating OHCI periodic endpoint.\n");
		goto fail_hcca;
	}
	memset((void *)periodic_ed, 0, sizeof(*periodic_ed));
	for (i = 0; i < 32; ++i)
		ohci->hcca->HccaInterruptTable[i] = __cpu_to_le32(virt_to_phys(periodic_ed));
	OHCI_INST (controller)->periodic_ed = periodic_ed;

	OHCI_INST (controller)->opreg->HcHCCA = __cpu_to_le32(virt_to_phys(OHCI_INST(controller)->hcca));
	/* Make sure periodic schedule is enabled. */
	OHCI_INST (controller)->opreg->HcControl |= __cpu_to_le32(PeriodicListEnable);
	OHCI_INST (controller)->opreg->HcControl &= __cpu_to_le32(~IsochronousEnable); // unused by this driver
	// disable everything, contrary to what OHCI spec says in 5.1.1.4, as we don't need IRQs
	OHCI_INST (controller)->opreg->HcInterruptEnable = __cpu_to_le32(1<<31);
	OHCI_INST (controller)->opreg->HcInterruptDisable = __cpu_to_le32(~(1<<31));
	OHCI_INST (controller)->opreg->HcInterruptStatus = __cpu_to_le32(~0);
	OHCI_INST (controller)->opreg->HcPeriodicStart =
		__cpu_to_le32((READ_OPREG(OHCI_INST(controller), HcFmInterval) & FrameIntervalMask) / 10 * 9);
	OHCI_INST (controller)->opreg->HcControl = __cpu_to_le32((READ_OPREG(OHCI_INST(controller), HcControl)
								& ~HostControllerFunctionalStateMask) | USBOperational);

	mdelay(100);

	controller->devices[0]->controller = controller;
	controller->devices[0]->init = ohci_rh_init;
	controller->devices[0]->init (controller->devices[0]);
	if (!controller->devices[0]->data) {
		printk("Could not initialize OHCI root hub.\n");
		goto fail_periodic;
	}
	return controller;

fail_periodic:
	ohci_reset(controller);
	free((void *)periodic_ed);
	OHCI_INST(controller)->periodic_ed = NULL;
fail_hcca:
	free((void *)OHCI_INST(controller)->hcca);
	OHCI_INST(controller)->hcca = NULL;
fail_root:
	usb_detach_device(controller, 0);
fail_instance:
	free(controller->instance);
	controller->instance = NULL;
fail_controller:
	detach_controller(controller);
	free(controller);
	return NULL;
}

hci_t *
ohci_pci_init (pci_addr addr)
{
	u32 bar0;
	u32 reg_base;
	u16 old_cmd;
	u16 cmd;
	hci_t *controller;

	bar0 = pci_config_read32(addr, PCI_BASE_ADDR_0);
	/* PCI_COMMAND_IO has the same bit value as BAR0's I/O-space indicator. */
	if (bar0 == 0 || bar0 == 0xffffffff || (bar0 & PCI_COMMAND_IO)) {
		usb_debug("Invalid OHCI BAR0: %08x\n", bar0);
		return NULL;
	}

	reg_base = bar0 & 0xfffff000U;
	if (!reg_base) {
		usb_debug("OHCI BAR0 has no MMIO base.\n");
		return NULL;
	}

	old_cmd = pci_config_read16(addr, PCI_COMMAND);
	cmd = old_cmd | PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER;
	if (cmd != old_cmd)
		pci_config_write16(addr, PCI_COMMAND, cmd);

	controller = ohci_init((void *)(unsigned long)reg_base);
	if (!controller && cmd != old_cmd)
		pci_config_write16(addr, PCI_COMMAND, old_cmd);

	return controller;
}

static void
ohci_shutdown (hci_t *controller)
{
	int i;

	if (controller == 0)
		return;
	detach_controller (controller);
	ohci_stop(controller);
	for (i = 127; i >= 0; --i) {
		if (controller->devices[i])
			usb_detach_device(controller, i);
	}
	controller->reset (controller);
	free ((void *)OHCI_INST (controller)->periodic_ed);
	free ((void *)OHCI_INST (controller)->hcca);
	free (OHCI_INST (controller));
	free (controller);
}

static void
ohci_start (__attribute__((unused)) hci_t *controller)
{
// TODO: turn on all operation of OHCI, but assume that it's initialized.
}

static void
ohci_stop (__attribute__((unused)) hci_t *controller)
{
// TODO: turn off all operation of OHCI
}

static int
wait_for_ed(usbdev_t *dev, ed_t *head, int pages)
{
	usb_debug("Waiting for %d pages on dev %p with head %p\n", pages, dev, head);
	/* wait for results */
	/* TOTEST: how long to wait?
	 *         give 2s per TD (2 pages) plus another 2s for now
	 */
	int timeout = pages*1000 + 2000;
	while (((__le32_to_cpu(head->head_pointer) & ~0xfU) !=
		(__le32_to_cpu(head->tail_pointer) & ~0xfU)) &&
		!(__le32_to_cpu(head->head_pointer) & 1) &&
		((__le32_to_cpu((((td_t*)phys_to_virt(__le32_to_cpu(head->head_pointer) & ~0xfU)))->config)
		& TD_CC_MASK) >= TD_CC_NOACCESS) && timeout--) {
		/* don't log every ms */
		if (!(timeout % 100))
		usb_debug("intst: %x; ctrl: %x; cmdst: %x; head: %x -> %x, tail: %x, condition: %x\n",
			  READ_OPREG(OHCI_INST(dev->controller), HcInterruptStatus),
			  READ_OPREG(OHCI_INST(dev->controller), HcControl),
			  READ_OPREG(OHCI_INST(dev->controller), HcCommandStatus),
			__le32_to_cpu(head->head_pointer),
			__le32_to_cpu(((td_t*)phys_to_virt(__le32_to_cpu(head->head_pointer) & ~0xfU))->next_td),
			__le32_to_cpu(head->tail_pointer),
			(__le32_to_cpu(((td_t*)phys_to_virt(__le32_to_cpu(head->head_pointer) & ~0xfU))->config) & TD_CC_MASK) >> TD_CC_SHIFT);
		mdelay(1);
	}
	if (timeout < 0) {
		usb_debug("Error: ohci: endpoint "
			"descriptor processing timed out.\n");
		ohci_process_done_queue(OHCI_INST(dev->controller), 1);
		return 1;
	}
	/* Clear the done queue. */
	ohci_process_done_queue(OHCI_INST(dev->controller), 1);

	if (__le32_to_cpu(head->head_pointer) & 1) {
		usb_debug("HALTED!\n");
		return 1;
	}
	return 0;
}

static void
ohci_free_td_chain(td_t *td)
{
	while (td) {
		u32 next = __le32_to_cpu(td->next_td) & ~0xfU;
		td_t *next_td = next ? (td_t *)phys_to_virt(next) : NULL;

		free((void *)td);
		td = next_td;
	}
}

static void
ohci_free_ed (ed_t *const head)
{
	/* In case the transfer canceled, we have to free unprocessed TDs. */
	while ((__le32_to_cpu(head->head_pointer) & ~0xfU) !=
			(__le32_to_cpu(head->tail_pointer) & ~0xfU)) {
		/* Save current TD pointer. */
		td_t *const cur_td =
			(td_t*)phys_to_virt(__le32_to_cpu(head->head_pointer) & ~0xfU);
		/* Advance head pointer. */
		head->head_pointer = cur_td->next_td;
		/* Free current TD. */
		free((void *)cur_td);
	}

	/* Always free the dummy TD */
	if ((__le32_to_cpu(head->head_pointer) & ~0xfU) ==
			(__le32_to_cpu(head->tail_pointer) & ~0xfU))
		free(phys_to_virt(__le32_to_cpu(head->head_pointer) & ~0xfU));
	/* and the ED. */
	free((void *)head);
}

static int
ohci_control (usbdev_t *dev, direction_t dir, int drlen, void *devreq, int dalen,
	      unsigned char *data)
{
	td_t *cur;
	int first_page = 0, last_page = 0, pages = 0;
	int transfer_pages;

	if (drlen <= 0 || !devreq || dalen < 0 || (dalen > 0 && !data))
		return 1;

	// pages are specified as 4K in OHCI, so don't use getpagesize()
	if (dalen > 0) {
		first_page = (unsigned long)data / 4096;
		last_page = (unsigned long)(data + dalen - 1) / 4096;
		if (last_page < first_page)
			last_page = first_page;
		pages = last_page - first_page + 1;
	}
	transfer_pages = pages;

	/* First TD. */
	td_t *first_td = ohci_alloc_aligned(sizeof(td_t), sizeof(td_t));
	if (!first_td)
		return 1;
	memset((void *)first_td, 0, sizeof(*first_td));
	cur = first_td;

	cur->config = __cpu_to_le32(TD_DIRECTION_SETUP |
		TD_DELAY_INTERRUPT_NOINTR |
		TD_TOGGLE_FROM_TD |
		TD_TOGGLE_DATA0 |
		TD_CC_NOACCESS);
	cur->current_buffer_pointer = __cpu_to_le32(virt_to_phys(devreq));
	cur->buffer_end = __cpu_to_le32(virt_to_phys((char *)devreq + drlen - 1));

	while (pages > 0) {
		/* One more TD. */
		td_t *next = ohci_alloc_aligned(sizeof(td_t), sizeof(td_t));
		if (!next) {
			ohci_free_td_chain(first_td);
			return 1;
		}
		memset((void *)next, 0, sizeof(*next));
		/* Linked to the previous. */
		cur->next_td = __cpu_to_le32(virt_to_phys(next));
		/* Advance to the new TD. */
		cur = next;

		cur->config = __cpu_to_le32((dir == IN ? TD_DIRECTION_IN : TD_DIRECTION_OUT) |
			TD_DELAY_INTERRUPT_NOINTR |
			TD_TOGGLE_FROM_ED |
			TD_CC_NOACCESS);
		cur->current_buffer_pointer = __cpu_to_le32(virt_to_phys(data));
		pages--;
		int consumed = (4096 - ((unsigned long)data % 4096));
		if (consumed >= dalen) {
			// end of data is within same page
			cur->buffer_end = __cpu_to_le32(virt_to_phys(data + dalen - 1));
			dalen = 0;
			/* assert(pages == 0); */
		} else {
			dalen -= consumed;
			data += consumed;
			pages--;
			int second_page_size = dalen;
			if (dalen > 4096) {
				second_page_size = 4096;
			}
			cur->buffer_end = __cpu_to_le32(virt_to_phys(data + second_page_size - 1));
			dalen -= second_page_size;
			data += second_page_size;
		}
	}

	/* One more TD. */
	td_t *next_td = ohci_alloc_aligned(sizeof(td_t), sizeof(td_t));
	if (!next_td) {
		ohci_free_td_chain(first_td);
		return 1;
	}
	memset((void *)next_td, 0, sizeof(*next_td));
	/* Linked to the previous. */
	cur->next_td = __cpu_to_le32(virt_to_phys(next_td));
	/* Advance to the new TD. */
	cur = next_td;
	cur->config = __cpu_to_le32((dir == IN ? TD_DIRECTION_OUT : TD_DIRECTION_IN) |
		TD_DELAY_INTERRUPT_ZERO | /* Write done head after this TD. */
		TD_TOGGLE_FROM_TD |
		TD_TOGGLE_DATA1 |
		TD_CC_NOACCESS);
	cur->current_buffer_pointer = 0;
	cur->buffer_end = 0;

	/* Final dummy TD. */
	td_t *final_td = ohci_alloc_aligned(sizeof(td_t), sizeof(td_t));
	if (!final_td) {
		ohci_free_td_chain(first_td);
		return 1;
	}
	memset((void *)final_td, 0, sizeof(*final_td));
	/* Linked to the previous. */
	cur->next_td = __cpu_to_le32(virt_to_phys(final_td));

	/* Data structures */
	ed_t *head = ohci_alloc_aligned(sizeof(ed_t), sizeof(ed_t));
	if (!head) {
		ohci_free_td_chain(first_td);
		return 1;
	}
	memset((void*)head, 0, sizeof(*head));
	head->config = __cpu_to_le32((dev->address << ED_FUNC_SHIFT) |
		(0 << ED_EP_SHIFT) |
		(OHCI_FROM_TD << ED_DIR_SHIFT) |
		(dev->speed?ED_LOWSPEED:0) |
		(dev->endpoints[0].maxpacketsize << ED_MPS_SHIFT));
	head->tail_pointer = __cpu_to_le32(virt_to_phys(final_td));
	head->head_pointer = __cpu_to_le32(virt_to_phys(first_td));

	usb_debug("ohci_control(): doing transfer with %x. first_td at %x\n",
		__le32_to_cpu(head->config) & ED_FUNC_MASK, __le32_to_cpu(head->head_pointer));
#ifdef USB_DEBUG_ED
	dump_ed(head);
#endif

	/* activate schedule */
	OHCI_INST(dev->controller)->opreg->HcControlHeadED = __cpu_to_le32(virt_to_phys(head));
	OHCI_INST(dev->controller)->opreg->HcControl |= __cpu_to_le32(ControlListEnable);
	OHCI_INST(dev->controller)->opreg->HcCommandStatus = __cpu_to_le32(ControlListFilled);

	int failure = wait_for_ed(dev, head, transfer_pages);
	/* Wait some frames before and one after disabling list access. */
	mdelay(4);
	OHCI_INST(dev->controller)->opreg->HcControl &= __cpu_to_le32(~ControlListEnable);
	mdelay(1);

	/* free memory */
	ohci_free_ed(head);

	return failure;
}

/* finalize == 1: if data is of packet aligned size, add a zero length packet */
static int
ohci_bulk (endpoint_t *ep, int dalen, u8 *data, int finalize)
{
	int i;
	int first_page = 0, last_page = 0, pages = 0;
	int transfer_pages;
	usb_debug("bulk: %x bytes from %p, finalize: %x, maxpacketsize: %x\n", dalen, data, finalize, ep->maxpacketsize);

	if (dalen < 0 || ep->maxpacketsize <= 0 || (dalen > 0 && !data))
		return 1;
	if (dalen == 0 && !finalize)
		return 0;

	td_t *cur, *next;

	// pages are specified as 4K in OHCI, so don't use getpagesize()
	if (dalen > 0) {
		first_page = (unsigned long)data / 4096;
		last_page = (unsigned long)(data + dalen - 1) / 4096;
		if (last_page < first_page)
			last_page = first_page;
		pages = last_page - first_page + 1;
	}
	transfer_pages = pages;
	int td_count = (pages+1)/2;

	if (finalize && ((dalen % ep->maxpacketsize) == 0)) {
		td_count++;
	}

	/* First TD. */
	td_t *first_td = ohci_alloc_aligned(sizeof(td_t), sizeof(td_t));
	if (!first_td)
		return 1;
	memset((void *)first_td, 0, sizeof(*first_td));
	cur = next = first_td;

	for (i = 0; i < td_count; ++i) {
		/* Advance to next TD. */
		cur = next;
		cur->config = __cpu_to_le32((ep->direction == IN ? TD_DIRECTION_IN : TD_DIRECTION_OUT) |
                        TD_DELAY_INTERRUPT_NOINTR |
                        TD_TOGGLE_FROM_ED |
                        TD_CC_NOACCESS);
		if (dalen == 0) {
			/* magic TD for empty packet transfer */
			cur->current_buffer_pointer = 0;
			cur->buffer_end = 0;
		} else {
			cur->current_buffer_pointer = __cpu_to_le32(virt_to_phys(data));
			pages--;
			int consumed = (4096 - ((unsigned long)data % 4096));
			if (consumed >= dalen) {
				// end of data is within same page
				cur->buffer_end = __cpu_to_le32(virt_to_phys(data + dalen - 1));
				dalen = 0;
				/* assert(pages == finalize); */
			} else {
				dalen -= consumed;
				data += consumed;
				pages--;
				int second_page_size = dalen;
				if (dalen > 4096) {
					second_page_size = 4096;
				}
				cur->buffer_end = __cpu_to_le32(virt_to_phys(data + second_page_size - 1));
				dalen -= second_page_size;
				data += second_page_size;
			}
		}
		/* One more TD. */
		next = ohci_alloc_aligned(sizeof(td_t), sizeof(td_t));
		if (!next) {
			ohci_free_td_chain(first_td);
			return 1;
		}
		memset((void *)next, 0, sizeof(*next));
		/* Linked to the previous. */
		cur->next_td = __cpu_to_le32(virt_to_phys(next));
	}

	/* Write done head after last TD. */
	cur->config &= __cpu_to_le32(~TD_DELAY_INTERRUPT_MASK);
	/* Advance to final, dummy TD. */
	cur = next;

	/* Data structures */
	ed_t *head = ohci_alloc_aligned(sizeof(ed_t), sizeof(ed_t));
	if (!head) {
		ohci_free_td_chain(first_td);
		return 1;
	}
	memset((void*)head, 0, sizeof(*head));
	head->config = __cpu_to_le32((ep->dev->address << ED_FUNC_SHIFT) |
		((ep->endpoint & 0xf) << ED_EP_SHIFT) |
		(((ep->direction==IN)?OHCI_IN:OHCI_OUT) << ED_DIR_SHIFT) |
		(ep->dev->speed?ED_LOWSPEED:0) |
		(ep->maxpacketsize << ED_MPS_SHIFT));
	head->tail_pointer = __cpu_to_le32(virt_to_phys(cur));
	head->head_pointer = __cpu_to_le32(virt_to_phys(first_td) | (ep->toggle?ED_TOGGLE:0));

	usb_debug("doing bulk transfer with %x(%x). first_td at %lx, last %lx\n",
		__le32_to_cpu(head->config) & ED_FUNC_MASK,
		(__le32_to_cpu(head->config) & ED_EP_MASK) >> ED_EP_SHIFT,
		virt_to_phys(first_td), virt_to_phys(cur));

	/* activate schedule */
	OHCI_INST(ep->dev->controller)->opreg->HcBulkHeadED = __cpu_to_le32(virt_to_phys(head));
	OHCI_INST(ep->dev->controller)->opreg->HcControl |= __cpu_to_le32(BulkListEnable);
	OHCI_INST(ep->dev->controller)->opreg->HcCommandStatus = __cpu_to_le32(BulkListFilled);

	int failure = wait_for_ed(ep->dev, head, transfer_pages);
	/* Wait some frames before and one after disabling list access. */
	mdelay(4);
	OHCI_INST(ep->dev->controller)->opreg->HcControl &= __cpu_to_le32(~BulkListEnable);
	mdelay(1);

	ep->toggle = __le32_to_cpu(head->head_pointer) & ED_TOGGLE;

	/* free memory */
	ohci_free_ed(head);

	if (failure) {
		/* try cleanup */
		clear_stall(ep);
	}

	return failure;
}


struct _intr_queue;

struct _intrq_td {
	volatile td_t		td;
	u8			*data;
	struct _intrq_td	*next;
	struct _intr_queue	*intrq;
} __attribute__ ((packed));

struct _intr_queue {
	volatile ed_t		ed;
	struct _intrq_td	*head;
	struct _intrq_td	*tail;
	u8			*data;
	u8			*spare_data;
	int			reqsize;
	endpoint_t		*endp;
	unsigned int		remaining_tds;
	int			destroy;
};

typedef struct _intrq_td intrq_td_t;
typedef struct _intr_queue intr_queue_t;

#define INTRQ_TD_FROM_TD(x) ((intrq_td_t *)x)

static void
ohci_fill_intrq_td(intrq_td_t *const td, intr_queue_t *const intrq,
		   u8 *const data)
{
	memset(td, 0, sizeof(*td));
	td->td.config = __cpu_to_le32(TD_QUEUETYPE_INTR |
		(intrq->endp->direction == IN ? TD_DIRECTION_IN : TD_DIRECTION_OUT) |
		TD_DELAY_INTERRUPT_ZERO |
		TD_TOGGLE_FROM_ED |
		TD_CC_NOACCESS);
	td->td.current_buffer_pointer = __cpu_to_le32(virt_to_phys(data));
	td->td.buffer_end = __cpu_to_le32(virt_to_phys(data) + intrq->reqsize - 1);
	td->intrq = intrq;
	td->data = data;
}

static void
ohci_free_intr_td_chain(intrq_td_t *td)
{
	while (td) {
		u32 next = __le32_to_cpu(td->td.next_td) & ~0xfU;
		intrq_td_t *next_td = next ?
			INTRQ_TD_FROM_TD(phys_to_virt(next)) : NULL;

		free(td);
		td = next_td;
	}
}

/* create and hook-up an intr queue into device schedule */
static void *
ohci_create_intr_queue(endpoint_t *const ep, const int reqsize,
		       const int reqcount, const int reqtiming)
{
	int i, phase;
	size_t buffer_count;
	size_t buffer_size;
	intrq_td_t *first_td = NULL, *last_td = NULL;

	if (!ep || reqsize <= 0 || reqsize > 4096 || reqcount <= 0 || reqtiming <= 0)
		return NULL;
	if (reqtiming > 32 || (reqtiming & (reqtiming - 1)))
		return NULL;

	buffer_count = (size_t)reqcount + 1; /* (reqcount + 1) active/spare buffers */
	if (buffer_count > (size_t)-1 / (size_t)reqsize)
		return NULL;
	buffer_size = buffer_count * (size_t)reqsize;

	intr_queue_t *intrq = ohci_alloc_aligned(sizeof(ed_t), sizeof(*intrq));
	if (!intrq)
		return NULL;
	memset(intrq, 0, sizeof(*intrq));
	intrq->data = (u8 *)malloc(buffer_size);
	if (!intrq->data) {
		free(intrq);
		return NULL;
	}
	intrq->spare_data = intrq->data + (size_t)reqcount * (size_t)reqsize;
	intrq->reqsize = reqsize;
	intrq->endp = ep;

	/* Create #reqcount TDs. */
	u8 *cur_data = intrq->data;
	for (i = 0; i < reqcount; ++i) {
		intrq_td_t *td = ohci_alloc_aligned(sizeof(td_t), sizeof(*td));
		if (!td) {
			ohci_free_intr_td_chain(first_td);
			free(intrq->data);
			free(intrq);
			return NULL;
		}
		++intrq->remaining_tds;
		ohci_fill_intrq_td(td, intrq, cur_data);
		cur_data += reqsize;
		if (!first_td)
			first_td = td;
		else
			last_td->td.next_td = __cpu_to_le32(virt_to_phys(&td->td));
		last_td = td;
	}

	/* Create last, dummy TD. */
	intrq_td_t *dummy_td = ohci_alloc_aligned(sizeof(td_t), sizeof(*dummy_td));
	if (!dummy_td) {
		ohci_free_intr_td_chain(first_td);
		free(intrq->data);
		free(intrq);
		return NULL;
	}
	memset(dummy_td, 0, sizeof(*dummy_td));
	dummy_td->intrq = intrq;
	last_td->td.next_td = __cpu_to_le32(virt_to_phys(&dummy_td->td));
	last_td = dummy_td;

	/* Initialize ED. */
	intrq->ed.config =  __cpu_to_le32((ep->dev->address << ED_FUNC_SHIFT) |
		((ep->endpoint & 0xf) << ED_EP_SHIFT) |
		(((ep->direction == IN) ? OHCI_IN : OHCI_OUT) << ED_DIR_SHIFT) |
		(ep->dev->speed ? ED_LOWSPEED : 0) |
		(ep->maxpacketsize << ED_MPS_SHIFT));
	intrq->ed.tail_pointer = __cpu_to_le32(virt_to_phys(&last_td->td));
	intrq->ed.head_pointer = __cpu_to_le32(virt_to_phys(&first_td->td) | (ep->toggle ? ED_TOGGLE : 0));

#ifdef USB_DEBUG_ED
	dump_ed((ed_t *)&intrq->ed);
#endif
	/* Insert the ED at one consistent phase in the 32-frame table. */
	ohci_t *const ohci = OHCI_INST(ep->dev->controller);
	const u32 dummy_ptr = __cpu_to_le32(virt_to_phys(ohci->periodic_ed));
	for (phase = 0; phase < reqtiming; ++phase) {
		int phase_free = 1;

		for (i = phase; i < 32; i += reqtiming) {
			if (ohci->hcca->HccaInterruptTable[i] != dummy_ptr) {
				phase_free = 0;
				break;
			}
		}
		if (!phase_free)
			continue;

		for (i = phase; i < 32; i += reqtiming) {
			usb_debug("Placed endpoint %lx to %d\n", virt_to_phys(&intrq->ed), i);
			ohci->hcca->HccaInterruptTable[i] =
				__cpu_to_le32(virt_to_phys(&intrq->ed));
		}
		return intrq;
	}

	usb_debug("Error: Failed to place ohci interrupt endpoint "
		"descriptor into periodic table: no phase available\n");
	ohci_free_intr_td_chain(first_td);
	free(intrq->data);
	free(intrq);
	return NULL;
}

/* remove queue from device schedule, dropping all data that came in */
static void
ohci_destroy_intr_queue(endpoint_t *const ep, void *const q_)
{
	intr_queue_t *const intrq = (intr_queue_t *)q_;
	int i;
	int frame_timeout = 200;
	u16 frame;

	if (!ep || !intrq)
		return;

	ohci_t *const ohci = OHCI_INST(ep->dev->controller);

	/* Stop the controller from fetching this ED before unlinking it. */
	frame = __le16_to_cpu(ohci->hcca->HccaFrameNumber);
	intrq->ed.config |= __cpu_to_le32(ED_SKIP);

	/* Remove interrupt queue from periodic table. */
	for (i = 0; i < 32; ++i) {
		if (ohci->hcca->HccaInterruptTable[i] ==
				__cpu_to_le32(virt_to_phys(&intrq->ed)))
			ohci->hcca->HccaInterruptTable[i] =
				__cpu_to_le32(virt_to_phys(ohci->periodic_ed));
	}

	/* The HC can cache a periodic ED for the current frame. Wait until the
	 * HCCA frame counter advances before reclaiming its TD and data memory. */
	while (__le16_to_cpu(ohci->hcca->HccaFrameNumber) == frame &&
			frame_timeout--)
		udelay(10);
	if (frame_timeout < 0) {
		usb_debug("OHCI frame counter stalled while removing interrupt queue; "
			"avoid freeing DMA-owned memory.\n");
		intrq->destroy = 1;
		return;
	}

	/* Collect TDs that already reached the done queue. */
	ohci_process_done_queue(ohci, 1);

	/* Save data toggle before dismantling the ED. */
	ep->toggle = __le32_to_cpu(intrq->ed.head_pointer) & ED_TOGGLE;

	/* Free unprocessed TDs. */
	while ((__le32_to_cpu(intrq->ed.head_pointer) & ~0xfU) !=
			(__le32_to_cpu(intrq->ed.tail_pointer) & ~0xfU)) {
		td_t *const cur_td = (td_t *)phys_to_virt(
				__le32_to_cpu(intrq->ed.head_pointer) & ~0xfU);
		intrq->ed.head_pointer = cur_td->next_td;
		free(INTRQ_TD_FROM_TD(cur_td));
		if (intrq->remaining_tds)
			--intrq->remaining_tds;
	}
	/* Free final, dummy TD. */
	free(phys_to_virt(__le32_to_cpu(intrq->ed.head_pointer) & ~0xfU));
	/* Free data buffer, including the spare slot. */
	free(intrq->data);
	intrq->data = NULL;
	intrq->spare_data = NULL;

	/* Free TDs already fetched from the done queue. */
	while (intrq->head) {
		intrq_td_t *const cur_td = intrq->head;
		intrq->head = intrq->head->next;
		free(cur_td);
		if (intrq->remaining_tds)
			--intrq->remaining_tds;
	}
	intrq->tail = NULL;

	/* Any TD still owned by the controller is freed when it reaches the done queue. */
	intrq->destroy = 1;
	if (!intrq->remaining_tds)
		free(intrq);
}

/* read one intr-packet from queue, if available. extend the queue for new input.
   return NULL if nothing new available.
   Recommended use: while (data=poll_intr_queue(q)) process(data);
 */
static u8 *
ohci_poll_intr_queue(void *const q_)
{
	intr_queue_t *const intrq = (intr_queue_t *)q_;

	if (!intrq || intrq->destroy)
		return NULL;

	/* Process done queue first, then check if we have work to do. */
	ohci_process_done_queue(OHCI_INST(intrq->endp->dev->controller), 0);

	while (intrq->head) {
		intrq_td_t *const cur_td = intrq->head;
		u32 condition_code;
		u8 *data;

		intrq->head = cur_td->next;
		if (!intrq->head)
			intrq->tail = NULL;

		condition_code = __le32_to_cpu(cur_td->td.config) & TD_CC_MASK;
		data = cur_td->data;

		/* Requeue with the spare buffer so returned data is no longer DMA-owned. */
		intrq_td_t *const dummy_td =
			INTRQ_TD_FROM_TD(phys_to_virt(__le32_to_cpu(intrq->ed.tail_pointer)));
		ohci_fill_intrq_td(dummy_td, intrq, intrq->spare_data);
		intrq->spare_data = data;

		/* Reset all but intrq pointer (i.e. init as dummy). */
		memset(cur_td, 0, sizeof(*cur_td));
		cur_td->intrq = intrq;
		/* Insert into interrupt queue as dummy. */
		dummy_td->td.next_td = __cpu_to_le32(virt_to_phys(&cur_td->td));
		intrq->ed.tail_pointer = __cpu_to_le32(virt_to_phys(&cur_td->td));

		if (condition_code != TD_CC_NOERR) {
			usb_debug("Dropping failed OHCI interrupt packet, condition %u.\n",
					condition_code >> TD_CC_SHIFT);
			continue;
		}

		return data;
	}

	return NULL;
}

static void
ohci_process_done_queue(ohci_t *const ohci, const int spew_debug)
{
	/* Temporary queue of interrupt queue TDs (to reverse order). */
	intrq_td_t *temp_tdq = NULL;
	u32 phys_done_queue;
#ifdef CONFIG_DEBUG_USB
	int i = 0, j = 0;
#else
	(void)spew_debug;
#endif

	/* Check if done head has been written. */
	if (!(READ_OPREG(ohci, HcInterruptStatus) & WritebackDoneHead))
		return;
	/* Fetch current done head. Low bits are status/alignment, not address. */
	phys_done_queue = __le32_to_cpu(ohci->hcca->HccaDoneHead) & ~0xfU;
	/* Tell host controller, he may overwrite the done head pointer. */
	ohci->opreg->HcInterruptStatus = __cpu_to_le32(WritebackDoneHead);

	/* Process done queue (it's in reversed order). */
	while (phys_done_queue) {
		td_t *const done_td = (td_t *)phys_to_virt(phys_done_queue);

		/* Advance pointer to next TD. */
		phys_done_queue = __le32_to_cpu(done_td->next_td) & ~0xfU;

		switch (__le32_to_cpu(done_td->config) & TD_QUEUETYPE_MASK) {
		case TD_QUEUETYPE_ASYNC:
			/* Free processed async TDs. */
			free((void *)done_td);
			break;
		case TD_QUEUETYPE_INTR: {
			intrq_td_t *const td = INTRQ_TD_FROM_TD(done_td);
			intr_queue_t *const intrq = td->intrq;
			/* Check if the corresponding interrupt
			   queue is still being processed. */
			if (intrq->destroy) {
				unsigned int remaining;

				free(td);
				if (intrq->remaining_tds)
					--intrq->remaining_tds;
				remaining = intrq->remaining_tds;
				usb_debug("Freed TD from orphaned interrupt "
					  "queue, %d TDs remain.\n", remaining);
				if (!remaining)
					free(intrq);
			} else {
				/* Save done TD to be processed. */
				td->next = temp_tdq;
				temp_tdq = td;
			}
			break;
		}
		default:
			break;
		}
#ifdef CONFIG_DEBUG_USB
		++i;
#endif
	}
#ifdef CONFIG_DEBUG_USB
	if (spew_debug)
		usb_debug("Processed %d done TDs.\n", i);
#endif

	/* Process interrupt queue TDs in right order. */
	while (temp_tdq) {
		/* Save pointer of current TD and advance. */
		intrq_td_t *const cur_td = temp_tdq;
		temp_tdq = temp_tdq->next;

		/* The interrupt queue for the current TD. */
		intr_queue_t *const intrq = cur_td->intrq;
		/* Append to interrupt queue. */
		if (!intrq->head) {
			/* First element. */
			intrq->head = intrq->tail = cur_td;
		} else {
			/* Insert at tail. */
			intrq->tail->next = cur_td;
			intrq->tail = cur_td;
		}
		/* It's always the last element. */
		cur_td->next = NULL;
#ifdef CONFIG_DEBUG_USB
		++j;
#endif
	}
#ifdef CONFIG_DEBUG_USB
	if (spew_debug)
		usb_debug("processed %d done tds, %d intr tds thereof.\n", i, j);
#endif
}

int ob_usb_ohci_init (const char *path, uint32_t addr)
{
	hci_t *ctrl;
	int i;

	usb_debug("ohci_init: %s addr = %x\n", path, addr);
	ctrl = ohci_pci_init(addr);
	if (!ctrl)
		return 0;

	/* Init ports */
	usb_poll();

	/* Look for a keyboard */
	for (i = 0; i < 128; i++) {
		if (ctrl->devices[i] && ctrl->devices[i]->configuration) {
			configuration_descriptor_t *cd;
			interface_descriptor_t *intf;

			cd = (configuration_descriptor_t *)ctrl->devices[i]->configuration;
			intf = (interface_descriptor_t *)(ctrl->devices[i]->configuration + cd->bLength);
			usb_debug("Device at port %d is class %d\n", i, intf->bInterfaceClass);
			if (intf->bInterfaceClass == hid_device &&
			    intf->bInterfaceSubClass == hid_subclass_boot &&
			    intf->bInterfaceProtocol == hid_boot_proto_keyboard ) {
				break;
			}
		}
	}
	if ( i < 128 )
		ob_usb_hid_add_keyboard(path);

	return 1;
}
