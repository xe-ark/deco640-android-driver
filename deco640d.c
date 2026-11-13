#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <linux/usbdevice_fs.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define VID_XPPEN 0x28bd
#define PID_DECO640 0x2904

#define RAW_MAX_X 31998
#define RAW_MAX_Y 17998
#define RAW_MAX_PRESSURE 16383

static int g_screen_w = 1080;
static int g_screen_h = 2400;
static volatile sig_atomic_t g_running = 1;

static void handle_sig(int sig) {
    (void)sig;
    g_running = 0;
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static void msleep(int ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static void detect_screen_size(int *out_w, int *out_h) {
    FILE *fp = popen("wm size 2>/dev/null", "r");
    if (!fp) return;
    char line[128];
    int phys_w = 0, phys_h = 0, over_w = 0, over_h = 0;
    while (fgets(line, sizeof(line), fp)) {
        int w = 0, h = 0;
        if (sscanf(line, "Override size: %dx%d", &w, &h) == 2 && w > 0 && h > 0) {
            over_w = w;
            over_h = h;
        } else if (sscanf(line, "Physical size: %dx%d", &w, &h) == 2 && w > 0 && h > 0) {
            phys_w = w;
            phys_h = h;
        }
    }
    pclose(fp);
    if (over_w > 0 && over_h > 0) {
        *out_w = over_w;
        *out_h = over_h;
    } else if (phys_w > 0 && phys_h > 0) {
        *out_w = phys_w;
        *out_h = phys_h;
    }
}

static void emit(int fd, uint16_t type, uint16_t code, int32_t value) {
    struct input_event ie;
    memset(&ie, 0, sizeof(ie));
    ie.type = type;
    ie.code = code;
    ie.value = value;
    if (write(fd, &ie, sizeof(ie)) < 0) {
        // ignore transient uinput write errors
    }
}

static void syn(int fd) {
    emit(fd, EV_SYN, SYN_REPORT, 0);
}

static void setup_abs_res(int fd, uint16_t code, int32_t minv, int32_t maxv, int32_t res) {
    ioctl(fd, UI_SET_ABSBIT, code);
    struct uinput_abs_setup abs;
    memset(&abs, 0, sizeof(abs));
    abs.code = code;
    abs.absinfo.minimum = minv;
    abs.absinfo.maximum = maxv;
    abs.absinfo.resolution = res;
    ioctl(fd, UI_ABS_SETUP, &abs);
}

static void setup_abs(int fd, uint16_t code, int32_t minv, int32_t maxv) {
    setup_abs_res(fd, code, minv, maxv, 0);
}

static int create_uinput_stylus(void) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) return -1;

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_KEYBIT, BTN_TOOL_PEN);
    ioctl(fd, UI_SET_KEYBIT, BTN_TOOL_RUBBER);
    ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(fd, UI_SET_KEYBIT, BTN_STYLUS);
    ioctl(fd, UI_SET_KEYBIT, BTN_STYLUS2);
    ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT);

    setup_abs_res(fd, ABS_X, 0, g_screen_w - 1, 100);
    setup_abs_res(fd, ABS_Y, 0, g_screen_h - 1, 100);
    setup_abs(fd, ABS_PRESSURE, 0, RAW_MAX_PRESSURE);
    setup_abs(fd, ABS_TILT_X, -127, 127);
    setup_abs(fd, ABS_TILT_Y, -127, 127);

    // INPUT_PROP_POINTER keeps Android's hover cursor visible for an external tablet.
    ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_POINTER);

    struct uinput_setup us;
    memset(&us, 0, sizeof(us));
    us.id.bustype = BUS_USB;
    us.id.vendor = VID_XPPEN;
    us.id.product = 0x9901;
    us.id.version = 1;
    snprintf(us.name, sizeof(us.name), "Deco640-Stylus");
    ioctl(fd, UI_DEV_SETUP, &us);
    ioctl(fd, UI_DEV_CREATE);
    return fd;
}

static int create_uinput_touch(void) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) return -1;

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH);

    setup_abs(fd, ABS_MT_SLOT, 0, 1);
    setup_abs(fd, ABS_MT_TRACKING_ID, 0, 65535);
    setup_abs(fd, ABS_MT_POSITION_X, 0, g_screen_w - 1);
    setup_abs(fd, ABS_MT_POSITION_Y, 0, g_screen_h - 1);
    setup_abs(fd, ABS_MT_TOUCH_MAJOR, 0, 255);
    setup_abs(fd, ABS_MT_PRESSURE, 0, 255);

    ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);

    // BUS_VIRTUAL ensures Android maps this direct touchscreen to the internal display
    // instead of treating it as an unattached external touch monitor.
    struct uinput_setup us;
    memset(&us, 0, sizeof(us));
    us.id.bustype = BUS_VIRTUAL;
    us.id.vendor = 0;
    us.id.product = 0;
    us.id.version = 0;
    snprintf(us.name, sizeof(us.name), "Deco640-Touch");
    ioctl(fd, UI_DEV_SETUP, &us);
    ioctl(fd, UI_DEV_CREATE);
    return fd;
}

static int create_uinput_kbd(void) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) return -1;

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    const uint16_t keys[] = {
        KEY_LEFTCTRL, KEY_LEFTSHIFT, KEY_LEFTALT,
        KEY_Z, KEY_Y, KEY_C, KEY_V, KEY_X, KEY_A, KEY_S,
        KEY_EQUAL, KEY_MINUS, KEY_SPACE, KEY_ENTER, KEY_ESC,
        KEY_BACK, KEY_HOME, KEY_PAGEUP, KEY_PAGEDOWN,
        KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        ioctl(fd, UI_SET_KEYBIT, keys[i]);
    }

    struct uinput_setup us;
    memset(&us, 0, sizeof(us));
    us.id.bustype = BUS_USB;
    us.id.vendor = VID_XPPEN;
    us.id.product = 0x9904;
    us.id.version = 1;
    snprintf(us.name, sizeof(us.name), "Deco640-Keyboard");
    ioctl(fd, UI_DEV_SETUP, &us);
    ioctl(fd, UI_DEV_CREATE);
    return fd;
}

static void destroy_uinput(int fd) {
    if (fd >= 0) {
        ioctl(fd, UI_DEV_DESTROY);
        close(fd);
    }
}

static void send_key_combo(int kbd_fd, uint16_t mod, uint16_t key) {
    if (mod) {
        emit(kbd_fd, EV_KEY, mod, 1);
        syn(kbd_fd);
        msleep(8);
    }
    emit(kbd_fd, EV_KEY, key, 1);
    syn(kbd_fd);
    msleep(12);
    emit(kbd_fd, EV_KEY, key, 0);
    syn(kbd_fd);
    if (mod) {
        msleep(8);
        emit(kbd_fd, EV_KEY, mod, 0);
        syn(kbd_fd);
    }
}

static int g_mt_id = 100;

static void touch_down_at(int touch_fd, int x, int y) {
    int tid = ++g_mt_id;
    if (g_mt_id > 60000) g_mt_id = 100;
    emit(touch_fd, EV_ABS, ABS_MT_SLOT, 0);
    emit(touch_fd, EV_ABS, ABS_MT_TRACKING_ID, tid);
    emit(touch_fd, EV_ABS, ABS_MT_POSITION_X, x);
    emit(touch_fd, EV_ABS, ABS_MT_POSITION_Y, y);
    emit(touch_fd, EV_ABS, ABS_MT_TOUCH_MAJOR, 14);
    emit(touch_fd, EV_ABS, ABS_MT_PRESSURE, 60);
    emit(touch_fd, EV_KEY, BTN_TOUCH, 1);
    syn(touch_fd);
}

static void touch_move_to(int touch_fd, int x, int y) {
    emit(touch_fd, EV_ABS, ABS_MT_SLOT, 0);
    emit(touch_fd, EV_ABS, ABS_MT_POSITION_X, x);
    emit(touch_fd, EV_ABS, ABS_MT_POSITION_Y, y);
    emit(touch_fd, EV_ABS, ABS_MT_TOUCH_MAJOR, 14);
    emit(touch_fd, EV_ABS, ABS_MT_PRESSURE, 60);
    syn(touch_fd);
}

static void touch_up(int touch_fd) {
    emit(touch_fd, EV_ABS, ABS_MT_SLOT, 0);
    emit(touch_fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
    emit(touch_fd, EV_KEY, BTN_TOUCH, 0);
    syn(touch_fd);
}

static void touch_swipe(int touch_fd, int x, int y_start, int y_end) {
    const int steps = 16;
    touch_down_at(touch_fd, x, y_start);
    msleep(20);

    for (int i = 1; i <= steps; i++) {
        int y = y_start + (y_end - y_start) * i / steps;
        msleep(12);
        touch_move_to(touch_fd, x, y);
    }
    msleep(35);
    touch_up(touch_fd);
}

static int read_sysfs_hex_or_int(const char *path, int base) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char buf[64];
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return (int)strtol(buf, NULL, base);
}

static bool find_deco_usb(char *out_path, size_t len) {
    DIR *dir = opendir("/sys/bus/usb/devices");
    if (!dir) return false;
    struct dirent *ent;
    bool found = false;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char p_vid[256], p_pid[256], p_bus[256], p_dev[256];
        snprintf(p_vid, sizeof(p_vid), "/sys/bus/usb/devices/%s/idVendor", ent->d_name);
        snprintf(p_pid, sizeof(p_pid), "/sys/bus/usb/devices/%s/idProduct", ent->d_name);
        int vid = read_sysfs_hex_or_int(p_vid, 16);
        int pid = read_sysfs_hex_or_int(p_pid, 16);
        if (vid == VID_XPPEN && pid == PID_DECO640) {
            snprintf(p_bus, sizeof(p_bus), "/sys/bus/usb/devices/%s/busnum", ent->d_name);
            snprintf(p_dev, sizeof(p_dev), "/sys/bus/usb/devices/%s/devnum", ent->d_name);
            int bus = read_sysfs_hex_or_int(p_bus, 10);
            int dev = read_sysfs_hex_or_int(p_dev, 10);
            if (bus > 0 && dev > 0) {
                snprintf(out_path, len, "/dev/bus/usb/%03d/%03d", bus, dev);
                found = true;
                break;
            }
        }
    }
    closedir(dir);
    return found;
}

#define MAX_GRAB_FDS 16
static int grab_deco_evdevs(int *fds) {
    int count = 0;
    DIR *dir = opendir("/dev/input");
    if (!dir) return 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && count < MAX_GRAB_FDS) {
        if (strncmp(ent->d_name, "event", 5) != 0) continue;
        char path[256];
        snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        struct input_id id;
        if (ioctl(fd, EVIOCGID, &id) == 0 && id.vendor == VID_XPPEN && id.product == PID_DECO640) {
            if (ioctl(fd, EVIOCGRAB, 1) == 0) {
                fds[count++] = fd;
                continue;
            }
        }
        close(fd);
    }
    closedir(dir);
    return count;
}

static void map_coords(int32_t rx, int32_t ry, int rot, int *sx, int *sy) {
    if (rx < 0) rx = 0;
    if (rx > RAW_MAX_X) rx = RAW_MAX_X;
    if (ry < 0) ry = 0;
    if (ry > RAW_MAX_Y) ry = RAW_MAX_Y;

    int x = 0, y = 0;
    switch (rot) {
        case 90:
            x = (int)((int64_t)(RAW_MAX_Y - ry) * (g_screen_w - 1) / RAW_MAX_Y);
            y = (int)((int64_t)rx * (g_screen_h - 1) / RAW_MAX_X);
            break;
        case 180:
            x = (int)((int64_t)(RAW_MAX_X - rx) * (g_screen_w - 1) / RAW_MAX_X);
            y = (int)((int64_t)(RAW_MAX_Y - ry) * (g_screen_h - 1) / RAW_MAX_Y);
            break;
        case 270:
            x = (int)((int64_t)ry * (g_screen_w - 1) / RAW_MAX_Y);
            y = (int)((int64_t)(RAW_MAX_X - rx) * (g_screen_h - 1) / RAW_MAX_X);
            break;
        default: // 0
            x = (int)((int64_t)rx * (g_screen_w - 1) / RAW_MAX_X);
            y = (int)((int64_t)ry * (g_screen_h - 1) / RAW_MAX_Y);
            break;
    }
    if (x < 0) x = 0;
    if (x >= g_screen_w) x = g_screen_w - 1;
    if (y < 0) y = 0;
    if (y >= g_screen_h) y = g_screen_h - 1;
    *sx = x;
    *sy = y;
}

static int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void release_stylus(int stylus_fd, bool *pen_in_range, bool *stylus_down, bool *right_btn_down) {
    if (*right_btn_down) {
        emit(stylus_fd, EV_KEY, BTN_RIGHT, 0);
        *right_btn_down = false;
    }
    if (*stylus_down) {
        emit(stylus_fd, EV_KEY, BTN_TOUCH, 0);
        emit(stylus_fd, EV_ABS, ABS_PRESSURE, 0);
        *stylus_down = false;
    }
    if (*pen_in_range) {
        emit(stylus_fd, EV_KEY, BTN_TOOL_PEN, 0);
        *pen_in_range = false;
    }
    syn(stylus_fd);
}

static void run_device_loop(const char *usb_path, int rot, bool start_drawing_mode) {
    int usb_fd = open(usb_path, O_RDWR);
    if (usb_fd < 0) {
        perror("open usb");
        return;
    }

    // Claim vendor interface 2 (0x03 OUT / 0x83 IN) and detach any competing usbfs client.
    struct usbdevfs_disconnect_claim dc;
    memset(&dc, 0, sizeof(dc));
    dc.interface = 2;
    if (ioctl(usb_fd, USBDEVFS_DISCONNECT_CLAIM, &dc) < 0) {
        unsigned int iface = 2;
        if (ioctl(usb_fd, USBDEVFS_CLAIMINTERFACE, &iface) < 0) {
            perror("claim interface 2");
            close(usb_fd);
            return;
        }
    }

    // Vendor initialization report documented by OpenTabletDriver (Deco 640 / IT640).
    uint8_t init_buf[20] = {0x02, 0xB0, 0x04};
    struct usbdevfs_bulktransfer xfer;
    memset(&xfer, 0, sizeof(xfer));
    xfer.ep = 0x03;
    xfer.len = sizeof(init_buf);
    xfer.timeout = 1000;
    xfer.data = init_buf;
    if (ioctl(usb_fd, USBDEVFS_BULK, &xfer) < 0) {
        perror("send init 02 B0 04");
        close(usb_fd);
        return;
    }

    int stylus_fd = create_uinput_stylus();
    int touch_fd = create_uinput_touch();
    int kbd_fd = create_uinput_kbd();
    if (stylus_fd < 0 || touch_fd < 0 || kbd_fd < 0) {
        perror("create uinput devices");
        destroy_uinput(kbd_fd);
        destroy_uinput(touch_fd);
        destroy_uinput(stylus_fd);
        close(usb_fd);
        return;
    }

    int grab_fds[MAX_GRAB_FDS];
    int grab_count = grab_deco_evdevs(grab_fds);
    fprintf(stderr, "Connected %s (%dx%d, rot=%d, mode=%s, grabbed %d evdevs)\n",
            usb_path, g_screen_w, g_screen_h, rot,
            start_drawing_mode ? "DRAWING_STYLUS" : "TOUCH_SCROLL", grab_count);
    fflush(stderr);

    bool drawing_mode = start_drawing_mode;
    bool pen_in_range = false;
    bool stylus_down = false;
    bool touch_down = false;
    bool right_btn_down = false;

    bool btn2_held = false;
    bool btn2_scrolling = false;
    int btn2_anchor_sx = 0, btn2_anchor_sy = 0;

    uint8_t prev_aux = 0;
    uint64_t aux_hold_next_ms[8] = {0};
    int smooth_sx = -1, smooth_sy = -1;

    uint8_t pkt[12];
    while (g_running) {
        memset(&xfer, 0, sizeof(xfer));
        xfer.ep = 0x83;
        xfer.len = sizeof(pkt);
        xfer.timeout = 50;
        xfer.data = pkt;

        int r = ioctl(usb_fd, USBDEVFS_BULK, &xfer);
        uint64_t t = now_ms();

        if ((prev_aux & 0x04) && t >= aux_hold_next_ms[2]) {
            if (pen_in_range) {
                release_stylus(stylus_fd, &pen_in_range, &stylus_down, &right_btn_down);
                msleep(15);
            }
            touch_swipe(touch_fd, g_screen_w / 2, (int)(g_screen_h * 0.58), (int)(g_screen_h * 0.45));
            aux_hold_next_ms[2] = t + 180;
        }
        if ((prev_aux & 0x08) && t >= aux_hold_next_ms[3]) {
            if (pen_in_range) {
                release_stylus(stylus_fd, &pen_in_range, &stylus_down, &right_btn_down);
                msleep(15);
            }
            touch_swipe(touch_fd, g_screen_w / 2, (int)(g_screen_h * 0.42), (int)(g_screen_h * 0.55));
            aux_hold_next_ms[3] = t + 180;
        }

        if (r < 0) {
            if (errno == ETIMEDOUT || errno == EINTR) continue;
            perror("usb read");
            break;
        }
        if (r < 10) continue;

        uint8_t b1 = pkt[1];
        if (b1 == 0xB4 || b1 == 0xF2 || b1 == 0xF8 || b1 == 0xB1) continue;

        if (b1 == 0xC0) {
            smooth_sx = -1;
            smooth_sy = -1;
            if (btn2_held && !btn2_scrolling) {
                emit(stylus_fd, EV_KEY, BTN_RIGHT, 1);
                syn(stylus_fd);
                msleep(15);
                emit(stylus_fd, EV_KEY, BTN_RIGHT, 0);
                syn(stylus_fd);
            }
            btn2_held = false;
            btn2_scrolling = false;
            if (touch_down) {
                touch_up(touch_fd);
                touch_down = false;
            }
            release_stylus(stylus_fd, &pen_in_range, &stylus_down, &right_btn_down);
            continue;
        }

        if (b1 & 0x10) {
            uint8_t aux = pkt[2];
            uint8_t pressed = aux & (uint8_t)(~prev_aux);
            if (pressed & 0x01) send_key_combo(kbd_fd, KEY_LEFTCTRL, KEY_Z);          // 1: Undo
            if (pressed & 0x02) send_key_combo(kbd_fd, KEY_LEFTCTRL, KEY_Y);          // 2: Redo
            if (pressed & 0x04) {                                                     // 3: Scroll Down
                if (pen_in_range) {
                    release_stylus(stylus_fd, &pen_in_range, &stylus_down, &right_btn_down);
                    msleep(15);
                }
                touch_swipe(touch_fd, g_screen_w / 2, (int)(g_screen_h * 0.58), (int)(g_screen_h * 0.45));
                aux_hold_next_ms[2] = t + 300;
            }
            if (pressed & 0x08) {                                                     // 4: Scroll Up
                if (pen_in_range) {
                    release_stylus(stylus_fd, &pen_in_range, &stylus_down, &right_btn_down);
                    msleep(15);
                }
                touch_swipe(touch_fd, g_screen_w / 2, (int)(g_screen_h * 0.42), (int)(g_screen_h * 0.55));
                aux_hold_next_ms[3] = t + 300;
            }
            if (pressed & 0x10) {                                                     // 5: Toggle Mode
                drawing_mode = !drawing_mode;
                btn2_held = false;
                btn2_scrolling = false;
                if (touch_down) {
                    touch_up(touch_fd);
                    touch_down = false;
                }
                release_stylus(stylus_fd, &pen_in_range, &stylus_down, &right_btn_down);
                fprintf(stderr, "Mode -> %s\n", drawing_mode ? "DRAWING_STYLUS" : "TOUCH_SCROLL");
                fflush(stderr);
            }
            if (pressed & 0x20) send_key_combo(kbd_fd, KEY_LEFTCTRL, KEY_EQUAL);      // 6: Zoom In
            if (pressed & 0x40) send_key_combo(kbd_fd, KEY_LEFTCTRL, KEY_MINUS);      // 7: Zoom Out
            if (pressed & 0x80) send_key_combo(kbd_fd, 0, KEY_BACK);                  // 8: Android Back
            prev_aux = aux;
            continue;
        }

        int32_t rx = (int32_t)(pkt[2] | (pkt[3] << 8) | ((r > 10 ? pkt[10] : 0) << 16));
        int32_t ry = (int32_t)(pkt[4] | (pkt[5] << 8) | ((r > 11 ? pkt[11] : 0) << 16));
        int32_t pr = (int32_t)(pkt[6] | (pkt[7] << 8));
        int8_t tx = (int8_t)pkt[8];
        int8_t ty = (int8_t)pkt[9];

        int sx = 0, sy = 0;
        map_coords(rx, ry, rot, &sx, &sy);
        if (smooth_sx < 0) {
            smooth_sx = sx;
            smooth_sy = sy;
        } else {
            smooth_sx = (smooth_sx * 2 + sx * 3) / 5;
            smooth_sy = (smooth_sy * 2 + sy * 3) / 5;
        }
        sx = smooth_sx;
        sy = smooth_sy;

        bool tip = (b1 & 0x01) != 0;
        bool btn1 = (b1 & 0x02) != 0; // Pen button 1: Left click / text selection
        bool btn2 = (b1 & 0x04) != 0; // Pen button 2: Tap = Right click, Hold+drag = Scroll

        if (btn2 && !btn2_held) {
            btn2_held = true;
            btn2_scrolling = false;
            btn2_anchor_sx = sx;
            btn2_anchor_sy = sy;
        } else if (!btn2 && btn2_held) {
            if (btn2_scrolling) {
                if (touch_down) {
                    touch_up(touch_fd);
                    touch_down = false;
                }
            } else {
                if (!pen_in_range) {
                    emit(stylus_fd, EV_KEY, BTN_TOOL_PEN, 1);
                    pen_in_range = true;
                }
                emit(stylus_fd, EV_ABS, ABS_X, sx);
                emit(stylus_fd, EV_ABS, ABS_Y, sy);
                emit(stylus_fd, EV_KEY, BTN_RIGHT, 1);
                syn(stylus_fd);
                msleep(15);
                emit(stylus_fd, EV_KEY, BTN_RIGHT, 0);
                syn(stylus_fd);
            }
            btn2_held = false;
            btn2_scrolling = false;
        } else if (btn2 && btn2_held && !btn2_scrolling) {
            int dist = abs(sx - btn2_anchor_sx) + abs(sy - btn2_anchor_sy);
            if (dist > 20) {
                btn2_scrolling = true;
                release_stylus(stylus_fd, &pen_in_range, &stylus_down, &right_btn_down);
                msleep(12);
                touch_down_at(touch_fd, g_screen_w / 2, g_screen_h / 2);
                touch_down = true;
            }
        }

        if (btn2_scrolling) {
            int tx_pos = clamp_int(g_screen_w / 2 + (sx - btn2_anchor_sx) * 45 / 100, 20, g_screen_w - 20);
            int ty_pos = clamp_int(g_screen_h / 2 + (sy - btn2_anchor_sy) * 45 / 100, 100, g_screen_h - 100);
            touch_move_to(touch_fd, tx_pos, ty_pos);
            continue;
        }

        if (!drawing_mode && !btn1) {
            if (tip) {
                if (!touch_down) {
                    if (pen_in_range) {
                        release_stylus(stylus_fd, &pen_in_range, &stylus_down, &right_btn_down);
                        msleep(8);
                    }
                    touch_down_at(touch_fd, sx, sy);
                    touch_down = true;
                } else {
                    touch_move_to(touch_fd, sx, sy);
                }
            } else {
                if (touch_down) {
                    touch_up(touch_fd);
                    touch_down = false;
                }
                if (!pen_in_range) {
                    emit(stylus_fd, EV_KEY, BTN_TOOL_PEN, 1);
                    pen_in_range = true;
                }
                emit(stylus_fd, EV_ABS, ABS_X, sx);
                emit(stylus_fd, EV_ABS, ABS_Y, sy);
                emit(stylus_fd, EV_ABS, ABS_PRESSURE, 0);
                emit(stylus_fd, EV_ABS, ABS_TILT_X, tx);
                emit(stylus_fd, EV_ABS, ABS_TILT_Y, ty);
                syn(stylus_fd);
            }
        } else {
            if (touch_down) {
                touch_up(touch_fd);
                touch_down = false;
            }
            bool left_active = tip || btn1;
            int32_t out_pr = tip ? pr : (btn1 ? (RAW_MAX_PRESSURE / 2) : 0);
            if (!pen_in_range) {
                emit(stylus_fd, EV_KEY, BTN_TOOL_PEN, 1);
                pen_in_range = true;
            }
            emit(stylus_fd, EV_ABS, ABS_X, sx);
            emit(stylus_fd, EV_ABS, ABS_Y, sy);
            emit(stylus_fd, EV_ABS, ABS_PRESSURE, out_pr);
            emit(stylus_fd, EV_ABS, ABS_TILT_X, tx);
            emit(stylus_fd, EV_ABS, ABS_TILT_Y, ty);
            if (left_active != stylus_down) {
                emit(stylus_fd, EV_KEY, BTN_TOUCH, left_active ? 1 : 0);
                stylus_down = left_active;
            }
            syn(stylus_fd);
        }
    }

    if (touch_down) touch_up(touch_fd);
    release_stylus(stylus_fd, &pen_in_range, &stylus_down, &right_btn_down);

    for (int i = 0; i < grab_count; i++) {
        ioctl(grab_fds[i], EVIOCGRAB, 0);
        close(grab_fds[i]);
    }
    destroy_uinput(kbd_fd);
    destroy_uinput(touch_fd);
    destroy_uinput(stylus_fd);
    close(usb_fd);
    fprintf(stderr, "Disconnected cleanly\n");
    fflush(stderr);
}

static void print_usage(const char *prog) {
    printf("Usage: %s [--rot 0|90|180|270] [--w WIDTH] [--h HEIGHT] [--drawing]\n", prog);
}

int main(int argc, char **argv) {
    int rot = 270;
    int custom_w = 0, custom_h = 0;
    bool start_drawing = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--rot") == 0 && i + 1 < argc) {
            rot = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--w") == 0 && i + 1 < argc) {
            custom_w = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--h") == 0 && i + 1 < argc) {
            custom_h = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--drawing") == 0) {
            start_drawing = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    detect_screen_size(&g_screen_w, &g_screen_h);
    if (custom_w > 0) g_screen_w = custom_w;
    if (custom_h > 0) g_screen_h = custom_h;

    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);

    char usb_path[128];
    while (g_running) {
        if (find_deco_usb(usb_path, sizeof(usb_path))) {
            run_device_loop(usb_path, rot, start_drawing);
        }
        if (g_running) msleep(500);
    }
    return 0;
}
