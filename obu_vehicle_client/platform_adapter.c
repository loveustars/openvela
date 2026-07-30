#include "platform_adapter.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/input/touchscreen.h>
#include <nuttx/video/fb.h>
#include <nuttx/video/rgbcolors.h>

typedef enum {
    UI_ACTION_NONE = 0,
    UI_ACTION_FROM_E,
    UI_ACTION_FROM_S,
    UI_ACTION_FROM_W,
    UI_ACTION_FROM_N,
    UI_ACTION_TO_LEFT,
    UI_ACTION_TO_STRAIGHT,
    UI_ACTION_TO_RIGHT,
    UI_ACTION_ENTER,
    UI_ACTION_LEAVE
} ObuUiAction;

typedef struct {
    int x;
    int y;
    int w;
    int h;
    const char *label;
    ObuUiAction action;
} UiButton;

typedef struct {
    int fb_fd;
    int touch_fd;
    struct fb_videoinfo_s vinfo;
    struct fb_planeinfo_s pinfo;
    uint16_t *fbmem;
    bool ready;
    bool touch_down;
    int touch_x;
    int touch_y;
    VehicleGuiState *gui_state;
    VehicleState *vehicle_state;
    SpatState *spat_state;
    GuiInputEvent pending_event;
} ObuUiState;

static ObuUiState g_ui;

static const UiButton g_buttons[] = {
    {12, 312, 296, 62, "ENTER", UI_ACTION_ENTER},
    {58, 246, 56, 54, "E", UI_ACTION_FROM_E},
    {122, 246, 56, 54, "S", UI_ACTION_FROM_S},
    {186, 246, 56, 54, "W", UI_ACTION_FROM_W},
    {250, 246, 56, 54, "N", UI_ACTION_FROM_N},
    {12, 180, 92, 54, "LEFT", UI_ACTION_TO_LEFT},
    {114, 180, 92, 54, "GO", UI_ACTION_TO_STRAIGHT},
    {216, 180, 92, 54, "RIGHT", UI_ACTION_TO_RIGHT},
    {12, 390, 296, 62, "LEAVE", UI_ACTION_LEAVE},
};

static unsigned long monotonic_timespec_to_ms(const struct timespec *ts)
{
    return (unsigned long)(ts->tv_sec * 1000UL + ts->tv_nsec / 1000000UL);
}

static uint16_t rgb24_to_rgb565(uint32_t color)
{
    return (((color >> 19) & 0x1f) << 11) |
           (((color >> 10) & 0x3f) << 5) |
           ((color >> 3) & 0x1f);
}

static void fb_update_full(void)
{
#if defined(CONFIG_FB_UPDATE)
    struct fb_area_s area;
    area.x = 0;
    area.y = 0;
    area.w = g_ui.vinfo.xres;
    area.h = g_ui.vinfo.yres;
    ioctl(g_ui.fb_fd, FBIO_UPDATE, (unsigned long)((uintptr_t)&area));
#endif
}

static void fb_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    int row;
    int col;
    int stride_pixels;

    if (!g_ui.fbmem) {
        return;
    }

    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > (int)g_ui.vinfo.xres) {
        w = g_ui.vinfo.xres - x;
    }
    if (y + h > (int)g_ui.vinfo.yres) {
        h = g_ui.vinfo.yres - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }

    stride_pixels = g_ui.pinfo.stride / 2;
    for (row = 0; row < h; row++) {
        uint16_t *line = g_ui.fbmem + (y + row) * stride_pixels + x;
        for (col = 0; col < w; col++) {
            line[col] = color;
        }
    }
}

static void fb_draw_rect_border(int x, int y, int w, int h, uint16_t color)
{
    fb_fill_rect(x, y, w, 2, color);
    fb_fill_rect(x, y + h - 2, w, 2, color);
    fb_fill_rect(x, y, 2, h, color);
    fb_fill_rect(x + w - 2, y, 2, h, color);
}

static void fb_draw_char(int x, int y, char c, uint16_t color, int scale)
{
    static const uint8_t glyphs[][7] = {
        [' '] = {0,0,0,0,0,0,0},
        ['-'] = {0,0,31,0,0,0,0},
        [':'] = {0,4,0,0,4,0,0},
        ['0'] = {14,17,19,21,25,17,14},
        ['1'] = {4,12,4,4,4,4,14},
        ['2'] = {14,17,1,2,4,8,31},
        ['3'] = {30,1,1,14,1,1,30},
        ['4'] = {2,6,10,18,31,2,2},
        ['5'] = {31,16,16,30,1,1,30},
        ['6'] = {14,16,16,30,17,17,14},
        ['7'] = {31,1,2,4,8,8,8},
        ['8'] = {14,17,17,14,17,17,14},
        ['9'] = {14,17,17,15,1,1,14},
        ['A'] = {14,17,17,31,17,17,17},
        ['B'] = {30,17,17,30,17,17,30},
        ['C'] = {14,17,16,16,16,17,14},
        ['D'] = {30,17,17,17,17,17,30},
        ['E'] = {31,16,16,30,16,16,31},
        ['F'] = {31,16,16,30,16,16,16},
        ['G'] = {14,17,16,23,17,17,14},
        ['H'] = {17,17,17,31,17,17,17},
        ['I'] = {31,4,4,4,4,4,31},
        ['L'] = {16,16,16,16,16,16,31},
        ['N'] = {17,25,21,19,17,17,17},
        ['O'] = {14,17,17,17,17,17,14},
        ['P'] = {30,17,17,30,16,16,16},
        ['R'] = {30,17,17,30,20,18,17},
        ['S'] = {15,16,16,14,1,1,30},
        ['T'] = {31,4,4,4,4,4,4},
        ['U'] = {17,17,17,17,17,17,14},
        ['V'] = {17,17,17,17,17,10,4},
        ['W'] = {17,17,17,21,21,21,10},
        ['Y'] = {17,17,10,4,4,4,4},
    };
    int row;
    int col;
    int dx;
    int dy;
    uint8_t bits;

    if (c < 0 || c > 127) {
        c = ' ';
    }

    for (row = 0; row < 7; row++) {
        bits = glyphs[(int)c][row];
        for (col = 0; col < 5; col++) {
            if (bits & (1 << (4 - col))) {
                for (dy = 0; dy < scale; dy++) {
                    for (dx = 0; dx < scale; dx++) {
                        fb_fill_rect(x + col * scale + dx, y + row * scale + dy, 1, 1, color);
                    }
                }
            }
        }
    }
}

static void fb_draw_text(int x, int y, const char *text, uint16_t color, int scale)
{
    int cursor = x;
    char c;

    while (text && *text) {
        c = *text++;
        if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';
        }
        fb_draw_char(cursor, y, c, color, scale);
        cursor += 6 * scale;
    }
}

static void ui_set_scaled_touch(int fd)
{
    int scaled = 1;
    ioctl(fd, TSIOC_USESCALED, (unsigned long)((uintptr_t)&scaled));
}

static void ui_apply_action(ObuUiAction action)
{
    memset(&g_ui.pending_event, 0, sizeof(g_ui.pending_event));

    switch (action) {
    case UI_ACTION_FROM_E:
        g_ui.pending_event.select_from_direction = true;
        g_ui.pending_event.from_direction = CARDINAL_EAST;
        break;
    case UI_ACTION_FROM_S:
        g_ui.pending_event.select_from_direction = true;
        g_ui.pending_event.from_direction = CARDINAL_SOUTH;
        break;
    case UI_ACTION_FROM_W:
        g_ui.pending_event.select_from_direction = true;
        g_ui.pending_event.from_direction = CARDINAL_WEST;
        break;
    case UI_ACTION_FROM_N:
        g_ui.pending_event.select_from_direction = true;
        g_ui.pending_event.from_direction = CARDINAL_NORTH;
        break;
    case UI_ACTION_TO_LEFT:
        g_ui.pending_event.select_turn_direction = true;
        g_ui.pending_event.turn_direction = TURN_LEFT;
        break;
    case UI_ACTION_TO_STRAIGHT:
        g_ui.pending_event.select_turn_direction = true;
        g_ui.pending_event.turn_direction = TURN_STRAIGHT;
        break;
    case UI_ACTION_TO_RIGHT:
        g_ui.pending_event.select_turn_direction = true;
        g_ui.pending_event.turn_direction = TURN_RIGHT;
        break;
    case UI_ACTION_ENTER:
        g_ui.pending_event.press_enter = true;
        break;
    case UI_ACTION_LEAVE:
        g_ui.pending_event.press_leave = true;
        break;
    default:
        break;
    }

    if (g_ui.gui_state) {
        g_ui.gui_state->dirty = true;
    }
}

static void ui_poll_touch(void)
{
    struct touch_sample_s sample;
    ssize_t nbytes;
    int x;
    int y;
    size_t i;

    if (g_ui.touch_fd < 0) {
        return;
    }

    nbytes = read(g_ui.touch_fd, &sample, sizeof(sample));
    if (nbytes != sizeof(sample) || sample.npoints < 1) {
        return;
    }

    x = sample.point[0].x;
    y = sample.point[0].y;
    g_ui.touch_x = x;
    g_ui.touch_y = y;

    if (sample.point[0].flags & (TOUCH_DOWN | TOUCH_MOVE)) {
        g_ui.touch_down = true;
    }

    for (i = 0; i < sizeof(g_buttons) / sizeof(g_buttons[0]); i++) {
        const UiButton *btn = &g_buttons[i];
        if (x >= btn->x && x < btn->x + btn->w &&
            y >= btn->y && y < btn->y + btn->h) {
            if ((sample.point[0].flags & TOUCH_DOWN) ||
                ((sample.point[0].flags & TOUCH_UP) && g_ui.touch_down)) {
                ui_apply_action(btn->action);
                printf("[TOUCH] x=%d y=%d action=%d flags=0x%x\n",
                       x, y, btn->action, sample.point[0].flags);
            }
            break;
        }
    }

    if (sample.point[0].flags & TOUCH_UP) {
        g_ui.touch_down = false;
    }
}

static void ui_draw_button(const UiButton *btn)
{
    fb_fill_rect(btn->x, btn->y, btn->w, btn->h, rgb24_to_rgb565(RGB24_BLUE));
    fb_draw_rect_border(btn->x, btn->y, btn->w, btn->h, rgb24_to_rgb565(RGB24_WHITE));
    if (btn->w >= 280) {
        fb_draw_text(btn->x + 112, btn->y + 20, btn->label, rgb24_to_rgb565(RGB24_WHITE), 2);
    } else {
        fb_draw_text(btn->x + 6, btn->y + 18, btn->label, rgb24_to_rgb565(RGB24_WHITE), 2);
    }
}

static void ui_draw(void)
{
    char line[80];
    size_t i;

    if (!g_ui.ready) {
        return;
    }

    fb_fill_rect(0, 0, g_ui.vinfo.xres, g_ui.vinfo.yres, rgb24_to_rgb565(0x103030));
    fb_fill_rect(6, 6, 308, 468, rgb24_to_rgb565(0x101820));
    fb_draw_rect_border(6, 6, 308, 468, rgb24_to_rgb565(RGB24_WHITE));

    fb_draw_text(18, 18, "OBU CLIENT", rgb24_to_rgb565(RGB24_YELLOW), 3);

    fb_draw_text(18, 58, "RSU:A+B", rgb24_to_rgb565(0x00ffcc), 2);

    snprintf(line, sizeof(line), "FROM:%s TO:%s",
             cardinal_direction_to_string(g_ui.gui_state->selected_from_direction),
             turn_direction_to_string(g_ui.gui_state->selected_turn_direction));
    fb_draw_text(18, 90, line, rgb24_to_rgb565(0x66ccff), 2);

    snprintf(line, sizeof(line), "ENTERED:%s PASSED:%s",
             g_ui.vehicle_state->has_entered_and_reported ? "YES" : "NO",
             g_ui.vehicle_state->has_passed ? "YES" : "NO");
    fb_draw_text(18, 122, line, rgb24_to_rgb565(0xffcc33), 2);

    if (g_ui.spat_state != NULL && g_ui.spat_state->valid) {
        snprintf(line, sizeof(line), "LIGHT:%s", g_ui.spat_state->phase);
    } else {
        snprintf(line, sizeof(line), "LIGHT:RED");
    }
    fb_draw_text(188, 58, line, rgb24_to_rgb565(0xff6666), 2);

    if (g_ui.spat_state != NULL && g_ui.spat_state->valid) {
        snprintf(line, sizeof(line), "%d", g_ui.spat_state->remain_s);
    } else {
        snprintf(line, sizeof(line), "--");
    }
    fb_draw_text(260, 86, line, rgb24_to_rgb565(0x99ff99), 3);

    fb_draw_text(12, 264, "FRO", rgb24_to_rgb565(0xffcc33), 2);
    fb_draw_text(12, 214, "TO", rgb24_to_rgb565(0xffcc33), 2);

    for (i = 0; i < sizeof(g_buttons) / sizeof(g_buttons[0]); i++) {
        ui_draw_button(&g_buttons[i]);
    }

    fb_update_full();
}

bool platform_adapter_init(void)
{
    memset(&g_ui, 0, sizeof(g_ui));
    g_ui.fb_fd = -1;
    g_ui.touch_fd = -1;
    return true;
}

bool platform_adapter_gui_init(
    VehicleGuiState *gui_state,
    VehicleState *vehicle_state,
    SpatState *spat_state)
{
    if (g_ui.ready) {
        g_ui.gui_state = gui_state;
        g_ui.vehicle_state = vehicle_state;
        g_ui.spat_state = spat_state;
        ui_draw();
        return true;
    }

    g_ui.fb_fd = open("/dev/fb0", O_RDWR);
    if (g_ui.fb_fd < 0) {
        return false;
    }

    if (ioctl(g_ui.fb_fd, FBIOGET_VIDEOINFO,
              (unsigned long)((uintptr_t)&g_ui.vinfo)) < 0 ||
        ioctl(g_ui.fb_fd, FBIOGET_PLANEINFO,
              (unsigned long)((uintptr_t)&g_ui.pinfo)) < 0) {
        close(g_ui.fb_fd);
        g_ui.fb_fd = -1;
        return false;
    }

    if (g_ui.pinfo.bpp != 16 || g_ui.pinfo.fbmem == NULL) {
        close(g_ui.fb_fd);
        g_ui.fb_fd = -1;
        return false;
    }

    g_ui.fbmem = (uint16_t *)g_ui.pinfo.fbmem;
    g_ui.touch_fd = open("/dev/input0", O_RDONLY | O_NONBLOCK);
    if (g_ui.touch_fd >= 0) {
        ui_set_scaled_touch(g_ui.touch_fd);
    }
    g_ui.gui_state = gui_state;
    g_ui.vehicle_state = vehicle_state;
    g_ui.spat_state = spat_state;
    g_ui.ready = true;
    ui_draw();
    return true;
}

bool platform_adapter_network_ready(void)
{
    return true;
}

unsigned long platform_adapter_get_time_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return monotonic_timespec_to_ms(&ts);
}

unsigned long platform_adapter_get_unix_timestamp(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return platform_adapter_get_time_ms() / 1000UL;
    }

    return (unsigned long)ts.tv_sec;
}

void platform_adapter_sleep_ms(unsigned long delay_ms)
{
    usleep(delay_ms * 1000UL);
}

GuiInputEvent platform_adapter_read_gui_input(unsigned long now_ms)
{
    GuiInputEvent event;

    (void)now_ms;
    ui_poll_touch();
    memcpy(&event, &g_ui.pending_event, sizeof(event));
    memset(&g_ui.pending_event, 0, sizeof(g_ui.pending_event));
    return event;
}

bool platform_adapter_mock_receive_spat(unsigned long now_ms, SpatState *spat_state)
{
    (void)now_ms;
    (void)spat_state;
    return false;
}

void platform_adapter_gui_refresh(void)
{
    ui_draw();
}

bool platform_adapter_should_quit(void)
{
    return false;
}

void platform_adapter_shutdown(void)
{
    if (g_ui.touch_fd >= 0) {
        close(g_ui.touch_fd);
    }
    if (g_ui.fb_fd >= 0) {
        close(g_ui.fb_fd);
    }
    memset(&g_ui, 0, sizeof(g_ui));
    g_ui.fb_fd = -1;
    g_ui.touch_fd = -1;
}
