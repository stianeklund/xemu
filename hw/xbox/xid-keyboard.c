#include "xid.h"

// #define DEBUG_XID
#ifdef DEBUG_XID
#define DPRINTF printf
#else
#define DPRINTF(...)
#endif

#define USB_VENDOR_MICROSOFT 0x045e
#define XID_KEYBOARD_IN_EP 0x02

#define USB_XID_KEYBOARD(obj) \
    OBJECT_CHECK(USBXIDKeyboardState, (obj), TYPE_USB_XID_KEYBOARD)

static const USBDescIface desc_iface_xbox_keyboard = {
    .bInterfaceNumber = 0,
    .bNumEndpoints = 1,
    .bInterfaceClass = USB_CLASS_XID,
    .bInterfaceSubClass = 0x42,
    .bInterfaceProtocol = 0x00,
    .eps =
        (USBDescEndpoint[]){
            {
                .bEndpointAddress = USB_DIR_IN | XID_KEYBOARD_IN_EP,
                .bmAttributes = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize = 0x20,
                .bInterval = 4,
            },
        },
};

static const USBDescDevice desc_device_xbox_keyboard = {
    .bcdUSB = 0x0110,
    .bMaxPacketSize0 = 0x40,
    .bNumConfigurations = 1,
    .confs =
        (USBDescConfig[]){
            {
                .bNumInterfaces = 1,
                .bConfigurationValue = 1,
                .bmAttributes = USB_CFG_ATT_ONE,
                .bMaxPower = 50,
                .nif = 1,
                .ifs = &desc_iface_xbox_keyboard,
            },
        },
};

static const USBDescStrings desc_keyboard_strings = {
    [STR_MANUFACTURER] = "QEMU",
    [STR_PRODUCT]      = "Microsoft Xbox Debug Keyboard",
    [STR_SERIALNUMBER] = "1",
};

static const USBDesc desc_xbox_keyboard = {
    .id = {
        .idVendor          = USB_VENDOR_MICROSOFT,
        .idProduct         = 0x0B00,
        .bcdDevice         = 0x0100,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT,
        .iSerialNumber     = STR_SERIALNUMBER,
    },
    .full = &desc_device_xbox_keyboard,
    .str  = desc_keyboard_strings,
};

static const XIDDesc desc_xid_xbox_keyboard = {
    .bLength = 0x10,
    .bDescriptorType = USB_DT_XID,
    .bcdXid = 0x100,
    .bType = XID_DEVICETYPE_DEBUG_KEYBOARD,
    .bSubType = XID_DEVICESUBTYPE_KEYBOARD,
    .bMaxInputReportSize = sizeof(XIDKeyboardReport),
    .bMaxOutputReportSize = 1,
    .wAlternateProductIds = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF },
};

static void update_keyboard_input(USBXIDKeyboardState *s)
{
    const bool *kbd = SDL_GetKeyboardState(NULL);
    int key_count = 0;

    s->in_state.modifiers = 0;
    s->in_state.reserved = 0;
    memset(s->in_state.keys, 0, sizeof(s->in_state.keys));

    static const struct {
        SDL_Scancode sc;
        uint8_t hid_usage;
    } key_map[] = {
        { SDL_SCANCODE_A,             0x04 },
        { SDL_SCANCODE_B,             0x05 },
        { SDL_SCANCODE_C,             0x06 },
        { SDL_SCANCODE_D,             0x07 },
        { SDL_SCANCODE_E,             0x08 },
        { SDL_SCANCODE_F,             0x09 },
        { SDL_SCANCODE_G,             0x0A },
        { SDL_SCANCODE_H,             0x0B },
        { SDL_SCANCODE_I,             0x0C },
        { SDL_SCANCODE_J,             0x0D },
        { SDL_SCANCODE_K,             0x0E },
        { SDL_SCANCODE_L,             0x0F },
        { SDL_SCANCODE_M,             0x10 },
        { SDL_SCANCODE_N,             0x11 },
        { SDL_SCANCODE_O,             0x12 },
        { SDL_SCANCODE_P,             0x13 },
        { SDL_SCANCODE_Q,             0x14 },
        { SDL_SCANCODE_R,             0x15 },
        { SDL_SCANCODE_S,             0x16 },
        { SDL_SCANCODE_T,             0x17 },
        { SDL_SCANCODE_U,             0x18 },
        { SDL_SCANCODE_V,             0x19 },
        { SDL_SCANCODE_W,             0x1A },
        { SDL_SCANCODE_X,             0x1B },
        { SDL_SCANCODE_Y,             0x1C },
        { SDL_SCANCODE_Z,             0x1D },
        { SDL_SCANCODE_1,             0x1E },
        { SDL_SCANCODE_2,             0x1F },
        { SDL_SCANCODE_3,             0x20 },
        { SDL_SCANCODE_4,             0x21 },
        { SDL_SCANCODE_5,             0x22 },
        { SDL_SCANCODE_6,             0x23 },
        { SDL_SCANCODE_7,             0x24 },
        { SDL_SCANCODE_8,             0x25 },
        { SDL_SCANCODE_9,             0x26 },
        { SDL_SCANCODE_0,             0x27 },
        { SDL_SCANCODE_RETURN,        0x28 },
        { SDL_SCANCODE_ESCAPE,        0x29 },
        { SDL_SCANCODE_BACKSPACE,     0x2A },
        { SDL_SCANCODE_TAB,           0x2B },
        { SDL_SCANCODE_SPACE,         0x2C },
        { SDL_SCANCODE_MINUS,         0x2D },
        { SDL_SCANCODE_EQUALS,        0x2E },
        { SDL_SCANCODE_LEFTBRACKET,   0x2F },
        { SDL_SCANCODE_RIGHTBRACKET,  0x30 },
        { SDL_SCANCODE_BACKSLASH,     0x31 },
        { SDL_SCANCODE_SEMICOLON,     0x33 },
        { SDL_SCANCODE_APOSTROPHE,    0x34 },
        { SDL_SCANCODE_GRAVE,         0x35 },
        { SDL_SCANCODE_COMMA,         0x36 },
        { SDL_SCANCODE_PERIOD,        0x37 },
        { SDL_SCANCODE_SLASH,         0x38 },
        { SDL_SCANCODE_CAPSLOCK,      0x39 },
        { SDL_SCANCODE_F1,            0x3A },
        { SDL_SCANCODE_F2,            0x3B },
        { SDL_SCANCODE_F3,            0x3C },
        { SDL_SCANCODE_F4,            0x3D },
        { SDL_SCANCODE_F5,            0x3E },
        { SDL_SCANCODE_F6,            0x3F },
        { SDL_SCANCODE_F7,            0x40 },
        { SDL_SCANCODE_F8,            0x41 },
        { SDL_SCANCODE_F9,            0x42 },
        { SDL_SCANCODE_F10,           0x43 },
        { SDL_SCANCODE_F11,           0x44 },
        { SDL_SCANCODE_F12,           0x45 },
        { SDL_SCANCODE_PRINTSCREEN,   0x46 },
        { SDL_SCANCODE_SCROLLLOCK,    0x47 },
        { SDL_SCANCODE_PAUSE,         0x48 },
        { SDL_SCANCODE_INSERT,        0x49 },
        { SDL_SCANCODE_HOME,          0x4A },
        { SDL_SCANCODE_PAGEUP,        0x4B },
        { SDL_SCANCODE_DELETE,        0x4C },
        { SDL_SCANCODE_END,           0x4D },
        { SDL_SCANCODE_PAGEDOWN,      0x4E },
        { SDL_SCANCODE_RIGHT,         0x4F },
        { SDL_SCANCODE_LEFT,          0x50 },
        { SDL_SCANCODE_DOWN,          0x51 },
        { SDL_SCANCODE_UP,            0x52 },
        { SDL_SCANCODE_NUMLOCKCLEAR,  0x53 },
    };

    static const struct {
        SDL_Scancode sc;
        uint8_t bit;
    } modifier_map[] = {
        { SDL_SCANCODE_LCTRL,  0 },
        { SDL_SCANCODE_LSHIFT, 1 },
        { SDL_SCANCODE_LALT,   2 },
        { SDL_SCANCODE_LGUI,   3 },
        { SDL_SCANCODE_RCTRL,  4 },
        { SDL_SCANCODE_RSHIFT, 5 },
        { SDL_SCANCODE_RALT,   6 },
        { SDL_SCANCODE_RGUI,   7 },
    };

    for (int i = 0; i < (int)(sizeof(modifier_map) / sizeof(modifier_map[0])); i++) {
        if (kbd[modifier_map[i].sc]) {
            s->in_state.modifiers |= 1 << modifier_map[i].bit;
        }
    }

    for (int i = 0; i < (int)(sizeof(key_map) / sizeof(key_map[0])); i++) {
        if (kbd[key_map[i].sc]) {
            if (key_count >= XID_KEYBOARD_MAX_KEYS) {
                break;
            }
            s->in_state.keys[key_count++] = key_map[i].hid_usage;
        }
    }
}

static void usb_xid_keyboard_handle_data(USBDevice *dev, USBPacket *p)
{
    USBXIDKeyboardState *s = DO_UPCAST(USBXIDKeyboardState, dev, dev);

    DPRINTF("xid-keyboard handle_data 0x%x %d 0x%zx\n", p->pid, p->ep->nr,
            p->iov.size);

    switch (p->pid) {
    case USB_TOKEN_IN:
        if (p->ep->nr == XID_KEYBOARD_IN_EP) {
            update_keyboard_input(s);
            usb_packet_copy(p, &s->in_state, sizeof(s->in_state));
        } else {
            assert(false);
        }
        break;
    default:
        p->status = USB_RET_STALL;
        assert(false);
        break;
    }
}

static void usb_xid_keyboard_handle_control(USBDevice *dev, USBPacket *p,
                                              int request, int value,
                                              int index, int length,
                                              uint8_t *data)
{
    USBXIDKeyboardState *s = (USBXIDKeyboardState *)dev;

    DPRINTF("xid-keyboard handle_control 0x%x 0x%x\n", request, value);

    int ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        DPRINTF("xid-keyboard handled by usb_desc_handle_control: %d\n", ret);
        return;
    }

    switch (request) {
    case ClassInterfaceRequest | HID_GET_REPORT:
        DPRINTF("xid-keyboard GET_REPORT 0x%x\n", value);
        update_keyboard_input(s);
        if (value == 0x0100) {
            if (length > sizeof(s->in_state)) {
                length = sizeof(s->in_state);
            }
            memcpy(data, &s->in_state, length);
            p->actual_length = length;
        } else {
            p->status = USB_RET_STALL;
        }
        break;
    case ClassInterfaceOutRequest | HID_SET_REPORT:
        DPRINTF("xid-keyboard SET_REPORT 0x%x\n", value);
        if (value == 0x0200 && length == sizeof(s->out_state)) {
            memcpy(&s->out_state, data, sizeof(s->out_state));
            p->actual_length = length;
        } else {
            p->status = USB_RET_STALL;
        }
        break;
    case VendorInterfaceRequest | USB_REQ_GET_DESCRIPTOR:
        DPRINTF("xid-keyboard GET_DESCRIPTOR 0x%x\n", value);
        if (value == 0x4200) {
            assert(s->xid_desc->bLength <= length);
            memcpy(data, s->xid_desc, s->xid_desc->bLength);
            p->actual_length = s->xid_desc->bLength;
        } else {
            p->status = USB_RET_STALL;
        }
        break;
    case VendorInterfaceRequest | XID_GET_CAPABILITIES:
        DPRINTF("xid-keyboard XID_GET_CAPABILITIES 0x%x\n", value);
        p->status = USB_RET_STALL;
        break;
    default:
        DPRINTF("xid-keyboard USB stalled on request 0x%x value 0x%x\n",
                request, value);
        p->status = USB_RET_STALL;
        break;
    }
}

static void usb_xid_keyboard_realize(USBDevice *dev, Error **errp)
{
    USBXIDKeyboardState *s = USB_XID_KEYBOARD(dev);
    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    s->intr = usb_ep_get(dev, USB_TOKEN_IN, 2);

    s->xid_desc = &desc_xid_xbox_keyboard;

    s->in_state.modifiers = 0;
    s->in_state.reserved = 0;
    memset(s->in_state.keys, 0, sizeof(s->in_state.keys));
    s->out_state = 0;
}

static void usb_xid_keyboard_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->product_desc = "Microsoft Xbox Debug Keyboard";
    uc->usb_desc = &desc_xbox_keyboard;
    uc->realize = usb_xid_keyboard_realize;
    uc->unrealize = usb_xbox_gamepad_unrealize;
    uc->handle_reset = usb_xid_handle_reset;
    uc->handle_control = usb_xid_keyboard_handle_control;
    uc->handle_data = usb_xid_keyboard_handle_data;
    uc->handle_attach = usb_desc_attach;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    dc->desc = "Microsoft Xbox Debug Keyboard";
}

static const TypeInfo usb_xbox_keyboard_info = {
    .name = TYPE_USB_XID_KEYBOARD,
    .parent = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBXIDKeyboardState),
    .class_init = usb_xid_keyboard_class_init,
};

static void usb_xid_keyboard_register_types(void)
{
    type_register_static(&usb_xbox_keyboard_info);
}

type_init(usb_xid_keyboard_register_types)
