/*
 * Driver for HID devices ported from CoreBoot
 *
 * Copyright (C) 2014 BALATON Zoltan
 *
 * This file was part of the libpayload project.
 *
 * Copyright (C) 2008-2010 coresystems GmbH
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

#include "config.h"
#include "libopenbios/bindings.h"
#include <libc/string.h>
#include "libc/byteorder.h"
#include "libc/vsprintf.h"
#include "drivers/usb.h"
#include "usb.h"

DECLARE_UNNAMED_NODE(usb_kbd, 0, sizeof(int));

static void
keyboard_open(__attribute__((unused)) int *idx)
{
	RET(-1);
}

static void
keyboard_close(__attribute__((unused)) int *idx)
{
}

static void keyboard_read(void);

NODE_METHODS( usb_kbd ) = {
	{ "open",		keyboard_open		},
	{ "close",		keyboard_close		},
	{ "read",               keyboard_read		},
};

#ifdef CONFIG_DEBUG_USB
static const char *boot_protos[3] = { "(none)", "keyboard", "mouse" };
#endif
typedef enum { hid_proto_boot = 0, hid_proto_report = 1 } hid_proto;
enum { GET_REPORT = 0x1, GET_IDLE = 0x2, GET_PROTOCOL = 0x3, SET_REPORT =
		0x9, SET_IDLE = 0xa, SET_PROTOCOL = 0xb
};

typedef union {
	struct {
		u8 modifiers;
		u8 reserved;
		u8 keys[6];
	};
	u8 buffer[8];
} usb_hid_keyboard_event_t;

#define HID_KEYBOARD_ERROR_ROLLOVER 0x01

static int
usb_hid_keyboard_rollover(const usb_hid_keyboard_event_t *event)
{
	int i;

	for (i = 0; i < 6; ++i) {
		if (event->keys[i] != HID_KEYBOARD_ERROR_ROLLOVER)
			return 0;
	}
	return 1;
}

struct layout_maps;

typedef struct {
	void *queue;
	hid_descriptor_t *descriptor;
	endpoint_t *endpoint;
	const struct layout_maps *map;

	usb_hid_keyboard_event_t previous;
	int lastkeypress;
	int repeat_delay;
} usbhid_inst_t;

#define HID_INST(dev) ((usbhid_inst_t*)(dev)->data)

static void
usb_hid_destroy (usbdev_t *dev)
{
	if (dev->data) {
		if (HID_INST(dev)->queue && HID_INST(dev)->endpoint) {
			dev->controller->destroy_intr_queue(
					HID_INST(dev)->endpoint, HID_INST(dev)->queue);
			HID_INST(dev)->queue = NULL;
		}
		HID_INST(dev)->endpoint = NULL;
		free(HID_INST(dev)->descriptor);
		HID_INST(dev)->descriptor = NULL;
		HID_INST(dev)->map = NULL;
		free(dev->data);
		dev->data = NULL;
	}

	free(dev->configuration);
	dev->configuration = NULL;
	free(dev->descriptor);
	dev->descriptor = NULL;
}

/* keybuffer is global to all USB keyboards */
static int keycount;
#define KEYBOARD_BUFFER_SIZE 16
static short keybuffer[KEYBOARD_BUFFER_SIZE];

static const char *const countries[36][2] = {
	{ "unknown", "us" },
	{ "Arabic", "ae" },
	{ "Belgian", "be" },
	{ "Canadian-Bilingual", "ca" },
	{ "Canadian-French", "ca" },
	{ "Czech Republic", "cz" },
	{ "Danish", "dk" },
	{ "Finnish", "fi" },
	{ "French", "fr" },
	{ "German", "de" },
	{ "Greek", "gr" },
	{ "Hebrew", "il" },
	{ "Hungary", "hu" },
	{ "International (ISO)", "iso" },
	{ "Italian", "it" },
	{ "Japan (Katakana)", "jp" },
	{ "Korean", "us" },
	{ "Latin American", "us" },
	{ "Netherlands/Dutch", "nl" },
	{ "Norwegian", "no" },
	{ "Persian (Farsi)", "ir" },
	{ "Poland", "pl" },
	{ "Portuguese", "pt" },
	{ "Russia", "ru" },
	{ "Slovakia", "sl" },
	{ "Spanish", "es" },
	{ "Swedish", "se" },
	{ "Swiss/French", "ch" },
	{ "Swiss/German", "ch" },
	{ "Switzerland", "ch" },
	{ "Taiwan", "tw" },
	{ "Turkish-Q", "tr" },
	{ "UK", "uk" },
	{ "US", "us" },
	{ "Yugoslavia", "yu" },
	{ "Turkish-F", "tr" },
	/* 36 - 255: Reserved */
};



struct layout_maps {
	const char *country;
	const short map[4][0x80];
};

#define KEY_BREAK     0x101  /* Not on PC KBD */
#define KEY_DOWN      0x102  /* Down arrow key */
#define KEY_UP        0x103  /* Up arrow key */
#define KEY_LEFT      0x104  /* Left arrow key */
#define KEY_RIGHT     0x105  /* Right arrow key */
#define KEY_HOME      0x106  /* home key */
#define KEY_BACKSPACE 0x107  /* not on pc */
#define KEY_F0        0x108  /* function keys; 64 reserved */
#define KEY_F(n)      (KEY_F0 + (n))

#define KEY_DC        0x14a  /* delete character */
#define KEY_IC        0x14b  /* insert char or enter ins mode */

#define KEY_NPAGE     0x152  /* next page */
#define KEY_PPAGE     0x153  /* previous page */

#define KEY_ENTER     0x157  /* enter or send (unreliable) */

#define KEY_PRINT     0x15a  /* print/copy */

#define KEY_END       0x166  /* end key */

static const struct layout_maps keyboard_layouts[] = {
// #ifdef CONFIG_PC_KEYBOARD_LAYOUT_US
{ .country = "us", .map = {
	{ /* No modifier */
	-1, -1, -1, -1, 'a', 'b', 'c', 'd',
	'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
	/* 0x10 */
	'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
	'u', 'v', 'w', 'x', 'y', 'z', '1', '2',
	/* 0x20 */
	'3', '4', '5', '6', '7', '8', '9', '0',
	'\n', '\e', '\b', '\t', ' ', '-', '=', '[',
	/* 0x30 */
	']', '\\', -1, ';', '\'', '`', ',', '.',
	'/', -1 /* CapsLk */, KEY_F(1), KEY_F(2), KEY_F(3), KEY_F(4), KEY_F(5), KEY_F(6),
	/* 0x40 */
	KEY_F(7), KEY_F(8), KEY_F(9), KEY_F(10), KEY_F(11), KEY_F(12), KEY_PRINT, -1 /* ScrLk */,
	KEY_BREAK, KEY_IC, KEY_HOME, KEY_PPAGE, KEY_DC, KEY_END, KEY_NPAGE, KEY_RIGHT,
	/* 50 */
	KEY_LEFT, KEY_DOWN, KEY_UP, -1 /*NumLck*/, '/', '*', '-' /* = ? */, '+',
	KEY_ENTER, KEY_END, KEY_DOWN, KEY_NPAGE, KEY_LEFT, -1, KEY_RIGHT, KEY_HOME,
	/* 60 */
	KEY_UP, KEY_PPAGE, -1, KEY_DC, -1 /* < > | */, -1 /* Win Key Right */, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	/* 70 */
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	 },
	{ /* Shift modifier */
	-1, -1, -1, -1, 'A', 'B', 'C', 'D',
	'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
	/* 0x10 */
	'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
	'U', 'V', 'W', 'X', 'Y', 'Z', '!', '@',
	/* 0x20 */
	'#', '$', '%', '^', '&', '*', '(', ')',
	'\n', '\e', '\b', '\t', ' ', '_', '+', '{',
	/* 0x30 */
	'}', '|', -1, ':', '"', '~', '<', '>',
	'?', -1 /* CapsLk */, KEY_F(1), KEY_F(2), KEY_F(3), KEY_F(4), KEY_F(5), KEY_F(6),
	/* 0x40 */
	KEY_F(7), KEY_F(8), KEY_F(9), KEY_F(10), KEY_F(11), KEY_F(12), KEY_PRINT, -1 /* ScrLk */,
	KEY_BREAK, KEY_IC, KEY_HOME, KEY_PPAGE, KEY_DC, KEY_END, KEY_NPAGE, KEY_RIGHT,
	/* 50 */
	KEY_LEFT, KEY_DOWN, KEY_UP, -1 /*NumLck*/, '/', '*', '-' /* = ? */, '+',
	KEY_ENTER, KEY_END, KEY_DOWN, KEY_NPAGE, KEY_LEFT, -1, KEY_RIGHT, KEY_HOME,
	/* 60 */
	KEY_UP, KEY_PPAGE, -1, KEY_DC, -1 /* < > | */, -1 /* Win Key Right */, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	/* 70 */
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	 },
	{ /* Alt */
	-1, -1, -1, -1, 'a', 'b', 'c', 'd',
	'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
	/* 0x10 */
	'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
	'u', 'v', 'w', 'x', 'y', 'z', '1', '2',
	/* 0x20 */
	'3', '4', '5', '6', '7', '8', '9', '0',
	'\n', '\e', '\b', '\t', ' ', '-', '=', '[',
	/* 0x30 */
	']', '\\', -1, ';', '\'', '`', ',', '.',
	'/', -1 /* CapsLk */, KEY_F(1), KEY_F(2), KEY_F(3), KEY_F(4), KEY_F(5), KEY_F(6),
	/* 0x40 */
	KEY_F(7), KEY_F(8), KEY_F(9), KEY_F(10), KEY_F(11), KEY_F(12), KEY_PRINT, -1 /* ScrLk */,
	KEY_BREAK, KEY_IC, KEY_HOME, KEY_PPAGE, KEY_DC, KEY_END, KEY_NPAGE, KEY_RIGHT,
	/* 50 */
	KEY_LEFT, KEY_DOWN, KEY_UP, -1 /*NumLck*/, '/', '*', '-' /* = ? */, '+',
	KEY_ENTER, KEY_END, KEY_DOWN, KEY_NPAGE, KEY_LEFT, -1, KEY_RIGHT, KEY_HOME,
	/* 60 */
	KEY_UP, KEY_PPAGE, -1, KEY_DC, -1 /* < > | */, -1 /* Win Key Right */, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	/* 70 */
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	 },
	{ /* Shift+Alt modifier */
	-1, -1, -1, -1, 'A', 'B', 'C', 'D',
	'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
	/* 0x10 */
	'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
	'U', 'V', 'W', 'X', 'Y', 'Z', '!', '@',
	/* 0x20 */
	'#', '$', '%', '^', '&', '*', '(', ')',
	'\n', '\e', '\b', '\t', ' ', '-', '=', '[',
	/* 0x30 */
	']', '\\', -1, ':', '\'', '`', ',', '.',
	'/', -1 /* CapsLk */, KEY_F(1), KEY_F(2), KEY_F(3), KEY_F(4), KEY_F(5), KEY_F(6),
	/* 0x40 */
	KEY_F(7), KEY_F(8), KEY_F(9), KEY_F(10), KEY_F(11), KEY_F(12), KEY_PRINT, -1 /* ScrLk */,
	KEY_BREAK, KEY_IC, KEY_HOME, KEY_PPAGE, KEY_DC, KEY_END, KEY_NPAGE, KEY_RIGHT,
	/* 50 */
	KEY_LEFT, KEY_DOWN, KEY_UP, -1 /*NumLck*/, '/', '*', '-' /* = ? */, '+',
	KEY_ENTER, KEY_END, KEY_DOWN, KEY_NPAGE, KEY_LEFT, -1, KEY_RIGHT, KEY_HOME,
	/* 60 */
	KEY_UP, KEY_PPAGE, -1, KEY_DC, -1 /* < > | */, -1 /* Win Key Right */, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	/* 70 */
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	 }
}},
//#endif
};

#define MOD_SHIFT    (1 << 0)
#define MOD_ALT      (1 << 1)
#define MOD_CTRL     (1 << 2)

static void usb_hid_keyboard_queue(int ch) {
	/* ignore key presses if buffer full */
	if (keycount < KEYBOARD_BUFFER_SIZE)
		keybuffer[keycount++] = ch;
}

#define KEYBOARD_REPEAT_MS	30
#define INITIAL_REPEAT_DELAY	10
#define REPEAT_DELAY		 2

static void
usb_hid_process_keyboard_event(usbhid_inst_t *const inst,
		const usb_hid_keyboard_event_t *const current)
{
	const usb_hid_keyboard_event_t *const previous = &inst->previous;

	int i, keypress = 0, modifiers = 0;

	if (current->modifiers & 0x01) /* Left-Ctrl */   modifiers |= MOD_CTRL;
	if (current->modifiers & 0x02) /* Left-Shift */  modifiers |= MOD_SHIFT;
	if (current->modifiers & 0x04) /* Left-Alt */    modifiers |= MOD_ALT;
	if (current->modifiers & 0x08) /* Left-GUI */    ;
	if (current->modifiers & 0x10) /* Right-Ctrl */  modifiers |= MOD_CTRL;
	if (current->modifiers & 0x20) /* Right-Shift */ modifiers |= MOD_SHIFT;
	if (current->modifiers & 0x40) /* Right-AltGr */ modifiers |= MOD_ALT;
	if (current->modifiers & 0x80) /* Right-GUI */   ;

	/* Did the event change at all? */
	if (inst->lastkeypress &&
			!memcmp(current, previous, sizeof(*current))) {
		/* No. Then it's a key repeat event. */
		if (inst->repeat_delay) {
			inst->repeat_delay--;
		} else {
			usb_hid_keyboard_queue(inst->lastkeypress);
			inst->repeat_delay = REPEAT_DELAY;
		}

		return;
	}

	inst->lastkeypress = 0;

	for (i=0; i<6; i++) {
		int j;
		int skip = 0;
		// No more keys? skip
		if (current->keys[i] == 0)
			return;
		if (current->keys[i] >= 0x80)
			continue;

		for (j=0; j<6; j++) {
			if (current->keys[i] == previous->keys[j]) {
				skip = 1;
				break;
			}
		}
		if (skip)
			continue;


		/* Mask off MOD_CTRL */
		keypress = inst->map->map[modifiers & 0x03][current->keys[i]];

		if (modifiers & MOD_CTRL) {
			switch (keypress) {
			case 'a' ... 'z':
			case 'A' ... 'Z':
				keypress &= 0x1f;
				break;
			default:
				continue;
			}
		}

		if (keypress == -1) {
			/* Debug: Print unknown keys */
			usb_debug ("usbhid: <%x> [ %x %x %x %x %x %x ] %d\n",
				current->modifiers,
				current->keys[0], current->keys[1],
				current->keys[2], current->keys[3],
				current->keys[4], current->keys[5], i);

			/* Unknown key? Try next one in the queue */
			continue;
		}

		usb_hid_keyboard_queue(keypress);

		/* Remember for authentic key repeat */
		inst->lastkeypress = keypress;
		inst->repeat_delay = INITIAL_REPEAT_DELAY;
	}
}

static void
usb_hid_poll (usbdev_t *dev)
{
	usb_hid_keyboard_event_t current;
	const u8 *buf;

	if (!dev->data || !HID_INST(dev)->queue)
		return;

	while ((buf=dev->controller->poll_intr_queue (HID_INST(dev)->queue))) {
		memcpy(current.buffer, buf, sizeof(current.buffer));
		/* Byte 1 of the boot keyboard input report is reserved. */
		current.reserved = 0;
		if (usb_hid_keyboard_rollover(&current)) {
			HID_INST(dev)->lastkeypress = 0;
			HID_INST(dev)->repeat_delay = 0;
			memset(&HID_INST(dev)->previous, 0,
					sizeof(HID_INST(dev)->previous));
			continue;
		}
		usb_hid_process_keyboard_event(HID_INST(dev), &current);
		HID_INST(dev)->previous = current;
	}
}

static int
usb_hid_set_idle (usbdev_t *dev, interface_descriptor_t *interface, u16 duration)
{
	dev_req_t dr;

	memset(&dr, 0, sizeof(dr));
	dr.data_dir = host_to_device;
	dr.req_type = class_type;
	dr.req_recp = iface_recp;
	dr.bRequest = SET_IDLE;
	dr.wValue = __cpu_to_le16((duration >> 2) << 8);
	dr.wIndex = __cpu_to_le16(interface->bInterfaceNumber);
	dr.wLength = 0;
	return dev->controller->control (dev, OUT, sizeof (dev_req_t), &dr, 0, 0);
}

static int
usb_hid_set_protocol (usbdev_t *dev, interface_descriptor_t *interface, hid_proto proto)
{
	dev_req_t dr;

	memset(&dr, 0, sizeof(dr));
	dr.data_dir = host_to_device;
	dr.req_type = class_type;
	dr.req_recp = iface_recp;
	dr.bRequest = SET_PROTOCOL;
	dr.wValue = __cpu_to_le16(proto);
	dr.wIndex = __cpu_to_le16(interface->bInterfaceNumber);
	dr.wLength = 0;
	return dev->controller->control (dev, OUT, sizeof (dev_req_t), &dr, 0, 0);
}

static int
usb_hid_queue_timing(usbdev_t *dev)
{
	int interval = HID_INST(dev)->endpoint->interval;

	/* Endpoint intervals are log2(microframes); OHCI schedules 1ms frames. */
	if (interval <= 3)
		return 1;
	interval -= 3;
	if (interval >= 5)
		return 32;
	return 1 << interval;
}

static int usb_hid_set_layout (usbhid_inst_t *inst, const char *country)
{
	unsigned int i;

	for (i = 0; i < (unsigned int)(sizeof(keyboard_layouts) / sizeof(keyboard_layouts[0])); i++) {
		if (strcmp(keyboard_layouts[i].country, country))
			continue;

		inst->map = &keyboard_layouts[i];
		usb_debug("  Keyboard layout '%s'\n", inst->map->country);
		return 0;
	}

	usb_debug("  Keyboard layout '%s' not found\n", country);
	return -1;
}

void
usb_hid_init (usbdev_t *dev)
{
	configuration_descriptor_t *cd = (configuration_descriptor_t*)dev->configuration;
	interface_descriptor_t *interface = (interface_descriptor_t*)(((char *) cd) + cd->bLength);

	/* HID owns descriptor/configuration cleanup even for unsupported HID variants. */
	dev->destroy = usb_hid_destroy;

	if (interface->bInterfaceSubClass == hid_subclass_boot) {
		u8 countrycode = 0;
		usb_debug ("  supports boot interface..\n");
#ifdef CONFIG_DEBUG_USB
		usb_debug ("  it's a %s\n",
			(interface->bInterfaceProtocol < 3) ?
			boot_protos[interface->bInterfaceProtocol] : "unknown");
#endif
		switch (interface->bInterfaceProtocol) {
		case hid_boot_proto_keyboard: {
			int i;
			int timing;

			dev->data = malloc (sizeof (usbhid_inst_t));
			if (!dev->data) {
				printk("Not enough memory for USB HID device.\n");
				return;
			}
			memset(dev->data, 0, sizeof(usbhid_inst_t));
			HID_INST(dev)->map = &keyboard_layouts[0];

			usb_debug ("  configuring...\n");
			if (usb_hid_set_protocol(dev, interface, hid_proto_boot)) {
				usb_debug("NOTICE: could not select USB HID boot protocol.\n");
				usb_detach_device(dev->controller, dev->address);
				return;
			}
			if (usb_hid_set_idle(dev, interface, KEYBOARD_REPEAT_MS)) {
				usb_debug("NOTICE: could not configure USB HID idle rate.\n");
				usb_detach_device(dev->controller, dev->address);
				return;
			}
			usb_debug ("  activating...\n");

			HID_INST(dev)->descriptor =
				(hid_descriptor_t *)get_descriptor(dev, gen_bmRequestType
					(device_to_host, standard_type, iface_recp),
					0x21, 0, interface->bInterfaceNumber);
			if (HID_INST(dev)->descriptor) {
				if (HID_INST(dev)->descriptor->bLength >= sizeof(hid_descriptor_t))
					countrycode = HID_INST(dev)->descriptor->bCountryCode;
				else {
					free(HID_INST(dev)->descriptor);
					HID_INST(dev)->descriptor = NULL;
				}
			}

			/* 35 countries defined: */
			if (countrycode > 35)
				countrycode = 0;
			usb_debug ("  Keyboard has %s layout (country code %02x)\n",
					countries[countrycode][0], countrycode);

			if (usb_hid_set_layout(HID_INST(dev), countries[countrycode][1]))
				HID_INST(dev)->map = &keyboard_layouts[0];

			for (i = 1; i < dev->num_endp; i++) {
				if (dev->endpoints[i].type != INTERRUPT)
					continue;
				if (dev->endpoints[i].direction != IN)
					continue;
				break;
			}
			if (i == dev->num_endp) {
				usb_debug("NOTICE: USB HID keyboard has no interrupt-IN endpoint.\n");
				usb_detach_device(dev->controller, dev->address);
				return;
			}
			HID_INST(dev)->endpoint = &dev->endpoints[i];
			if (HID_INST(dev)->endpoint->maxpacketsize <
					(int)sizeof(usb_hid_keyboard_event_t)) {
				usb_debug("NOTICE: USB HID keyboard interrupt endpoint is too small.\n");
				usb_detach_device(dev->controller, dev->address);
				return;
			}
			timing = usb_hid_queue_timing(dev);
			usb_debug ("  found endpoint %x for interrupt-in, polling every %dms\n",
					i, timing);
			/* 20 boot-report buffers, using the endpoint's normalized interval. */
			HID_INST(dev)->queue = dev->controller->create_intr_queue (
					HID_INST(dev)->endpoint,
					(int)sizeof(usb_hid_keyboard_event_t), 20, timing);
			if (!HID_INST(dev)->queue) {
				usb_debug("NOTICE: could not create USB HID interrupt queue.\n");
				usb_detach_device(dev->controller, dev->address);
				return;
			}
			// only add here, because we only support boot-keyboard HID devices
			dev->poll = usb_hid_poll;
			usb_debug ("  configuration done.\n");
			break;
		}
		default:
			usb_debug("NOTICE: HID interface protocol %d%s not supported.\n",
				 interface->bInterfaceProtocol,
				 (interface->bInterfaceProtocol == hid_boot_proto_mouse ?
				 " (USB mouse)" : ""));
			break;
		}
	}
}

static int usbhid_havechar (void)
{
	return (keycount != 0);
}

static int usbhid_getchar (void)
{
	short ret;

	if (keycount == 0)
		return 0;
	ret = keybuffer[0];
	--keycount;
	memmove(keybuffer, keybuffer + 1,
		keycount * sizeof(keybuffer[0]));

	return (int)ret;
}

/* ( addr len -- actual ) */
static void keyboard_read(void)
{
	char *addr;
	int len, key, i;

	usb_poll();
	len=POP();
	addr=(char *)cell2pointer(POP());

	for (i = 0; i < len; i++) {
	    if (!usbhid_havechar())
			break;
		key = usbhid_getchar();
		*addr++ = (char)key;
	}
	PUSH(i);
}

void ob_usb_hid_add_keyboard(const char *path)
{
	char name[128];
	phandle_t aliases;

	fword("new-device");

	push_str("keyboard");
	fword("device-name");

	push_str("keyboard");
	fword("device-type");

	snprintf(name, sizeof(name), "%s/keyboard", path);
	usb_debug("Found keyboard at %s\n", name);

	BIND_NODE_METHODS(get_cur_dev(), usb_kbd);

	fword("finish-device");

	aliases = find_dev("/aliases");
	set_property(aliases, "keyboard", name, strlen(name) + 1);
}
