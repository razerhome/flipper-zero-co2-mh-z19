
#include "co2_detector_mh_z19_icons.h"

#include <furi.h>
#include <furi_hal.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification_messages.h>

#include <string.h>

// --- Page navigation ---
#define PAGE_CONNECT   0
#define PAGE_CALIBRATE 1
#define PAGE_MEASURE   2
#define PAGE_DEBUG     3
#define PAGE_GRAPH     4

// --- Sensor ---
#define SENSOR_RANGE 5000

// --- Filter ---
#define PPM_BUFFER_SIZE 8
#define EMA_ALPHA       64
#define EMA_SCALE       256
#define STATUS_HYST     25

// --- Calibration ---
#define PPM_OFFSET_STEP    5
#define PPM_OFFSET_MIN     (-500)
#define PPM_OFFSET_MAX     500
#define PPM_OFFSET_DEFAULT 180

// --- History ---
#define HISTORY_SIZE           128
#define HISTORY_INITIAL_INTERVAL 5

// --- Graph ---
#define GRAPH_TOP    8
#define GRAPH_BOTTOM 55
#define GRAPH_HEIGHT (GRAPH_BOTTOM - GRAPH_TOP)
#define GRAPH_MIN_RANGE 100

typedef enum {
    GreenStatus,
    YellowStatus,
    RedStatus,
} StatusPPM;

typedef struct {
    int32_t buffer[PPM_BUFFER_SIZE];
    uint8_t head;
    uint8_t count;
    int32_t ema;
    int32_t display_ppm;
    StatusPPM display_status;
} PpmFilter;

typedef struct {
    int32_t data[HISTORY_SIZE];
    uint16_t head;
    uint16_t count;
    uint32_t sample_interval;
    uint32_t last_sample_tick;
} PpmHistory;

struct MHZ19App {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;
    FuriMutex* mutex;
    NotificationApp* notifications;

    bool have_5v;
    int32_t current_page;

    StatusPPM status_ppm;

    const GpioPin* input_pin;

    int32_t co2_ppm;
    int32_t ppm_offset;

    PpmFilter filter;
    PpmHistory history;

    // Debug info
    int32_t raw_ppm;
    int32_t dbg_th;
    int32_t dbg_tl;
    uint32_t read_count;
};

typedef struct MHZ19App MHZ19App;

// --- Notification sequences ---

const NotificationSequence green_led_sequence = {
    &message_green_255,
    &message_do_not_reset,
    NULL,
};

const NotificationSequence yellow_led_sequence = {
    &message_green_255,
    &message_red_255,
    &message_do_not_reset,
    &message_vibro_on,
    &message_note_c5,
    &message_delay_50,
    &message_vibro_off,
    &message_sound_off,
    NULL,
};

const NotificationSequence red_led_sequence = {
    &message_red_255,
    &message_do_not_reset,
    &message_vibro_on,
    &message_note_c5,
    &message_delay_50,
    &message_vibro_off,
    &message_sound_off,
    NULL,
};

// --- History functions ---

static void history_init(PpmHistory* h) {
    memset(h, 0, sizeof(PpmHistory));
    h->sample_interval = HISTORY_INITIAL_INTERVAL;
}

static void history_compress(PpmHistory* h) {
    uint16_t new_count = h->count / 2;
    for(uint16_t i = 0; i < new_count; i++) {
        h->data[i] = (h->data[i * 2] + h->data[i * 2 + 1]) / 2;
    }
    h->head = new_count;
    h->count = new_count;
    h->sample_interval *= 2;
}

static void history_push(PpmHistory* h, int32_t ppm) {
    if(ppm <= 0) return;
    uint32_t now = furi_get_tick();
    if(now - h->last_sample_tick < h->sample_interval * 1000) return;
    h->last_sample_tick = now;

    if(h->count >= HISTORY_SIZE) {
        history_compress(h);
    }
    h->data[h->head] = ppm;
    h->head++;
    h->count++;
}

// --- Drawing functions ---

static void draw_page_connect(Canvas* canvas) {
    canvas_set_font(canvas, FontPrimary);
    elements_multiline_text_aligned(
        canvas,
        4,
        1,
        AlignLeft,
        AlignTop,
        "Connect sensor MH-Z19 to pins:\n5V -> 1\nGND -> 8\nPWM -> 3\nPress "
        "center button...");
}

static void draw_page_calibrate(Canvas* canvas, MHZ19App* app) {
    FuriString* strbuf = furi_string_alloc();

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 4, 12, "Calibration offset:");

    furi_string_printf(strbuf, "%+ld ppm", app->ppm_offset);
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str(canvas, 20, 38, furi_string_get_cstr(strbuf));

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 4, 50, "Adjust with <- -> (step 5)");
    canvas_draw_str(canvas, 4, 60, "Press OK to start measuring");

    furi_string_free(strbuf);
}

static void draw_page_measure(Canvas* canvas, MHZ19App* app) {
    FuriString* strbuf = furi_string_alloc();

    const Icon* icon;
    const char* status_str;

    switch(app->status_ppm) {
    case GreenStatus:
        icon = &I_passport_okay1_46x49;
        status_str = "it's OK!";
        break;
    case YellowStatus:
        icon = &I_passport_bad1_46x49;
        status_str = "Not good!";
        break;
    case RedStatus:
        icon = &I_passport_bad3_46x49;
        status_str = "Very bad!";
        break;
    default:
        icon = &I_passport_okay1_46x49;
        status_str = "It's OK!";
        break;
    }

    canvas_draw_icon(canvas, 9, 7, icon);
    canvas_draw_icon(canvas, 59, 8, &I_co2);

    furi_string_printf(strbuf, "%ld", app->co2_ppm);
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str(canvas, 60, 40, furi_string_get_cstr(strbuf));

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 60, 55, status_str);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 110, 40, "ppm");

    furi_string_printf(strbuf, "%+ld", app->ppm_offset);
    canvas_draw_str(canvas, 104, 8, furi_string_get_cstr(strbuf));

    furi_string_free(strbuf);
}

static void draw_page_debug(Canvas* canvas, MHZ19App* app) {
    FuriString* strbuf = furi_string_alloc();

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 4, 12, "Debug info:");

    canvas_set_font(canvas, FontSecondary);

    furi_string_printf(strbuf, "Raw PPM:   %ld", app->raw_ppm);
    canvas_draw_str(canvas, 4, 24, furi_string_get_cstr(strbuf));

    furi_string_printf(strbuf, "Th (HIGH): %ld ms", app->dbg_th);
    canvas_draw_str(canvas, 4, 34, furi_string_get_cstr(strbuf));

    furi_string_printf(strbuf, "Tl (LOW):  %ld ms", app->dbg_tl);
    canvas_draw_str(canvas, 4, 44, furi_string_get_cstr(strbuf));

    furi_string_printf(strbuf, "Readings:  %lu", app->read_count);
    canvas_draw_str(canvas, 4, 54, furi_string_get_cstr(strbuf));

    furi_string_printf(strbuf, "Offset:    %+ld", app->ppm_offset);
    canvas_draw_str(canvas, 4, 64, furi_string_get_cstr(strbuf));

    furi_string_free(strbuf);
}

static void draw_page_graph(Canvas* canvas, MHZ19App* app) {
    FuriString* strbuf = furi_string_alloc();
    PpmHistory* h = &app->history;

    if(h->count < 2) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "Collecting data...");
        furi_string_free(strbuf);
        return;
    }

    // Find min/max
    int32_t y_min = h->data[0];
    int32_t y_max = h->data[0];
    for(uint16_t i = 1; i < h->count; i++) {
        if(h->data[i] < y_min) y_min = h->data[i];
        if(h->data[i] > y_max) y_max = h->data[i];
    }

    // Ensure minimum range
    int32_t range = y_max - y_min;
    if(range < GRAPH_MIN_RANGE) {
        int32_t mid = (y_max + y_min) / 2;
        y_min = mid - GRAPH_MIN_RANGE / 2;
        y_max = mid + GRAPH_MIN_RANGE / 2;
        if(y_min < 0) {
            y_min = 0;
            y_max = GRAPH_MIN_RANGE;
        }
        range = y_max - y_min;
    }

    // Add 10% padding
    int32_t padding = range / 10;
    if(padding < 10) padding = 10;
    y_min -= padding;
    y_max += padding;
    if(y_min < 0) y_min = 0;
    range = y_max - y_min;

    // --- Corner labels ---
    canvas_set_font(canvas, FontSecondary);

    int32_t data_min = y_min + padding;
    int32_t data_max = y_max - padding;

    // Top-left: max
    furi_string_printf(strbuf, "max:%ld", data_max);
    canvas_draw_str(canvas, 0, 7, furi_string_get_cstr(strbuf));

    // Top-center: battery
    furi_string_printf(strbuf, "%d%%", furi_hal_power_get_pct());
    canvas_draw_str_aligned(canvas, 64, 7, AlignCenter, AlignBottom, furi_string_get_cstr(strbuf));

    // Top-right: now
    furi_string_printf(strbuf, "now:%ld", app->co2_ppm);
    canvas_draw_str_aligned(canvas, 127, 7, AlignRight, AlignBottom, furi_string_get_cstr(strbuf));

    // Bottom-left: min
    furi_string_printf(strbuf, "min:%ld", data_min);
    canvas_draw_str(canvas, 0, 62, furi_string_get_cstr(strbuf));

    // Bottom-center: time span
    uint32_t total_seconds = h->count * h->sample_interval;
    if(total_seconds < 3600) {
        furi_string_printf(strbuf, "%lum", total_seconds / 60);
    } else {
        furi_string_printf(strbuf, "%luh", total_seconds / 3600);
    }
    canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, furi_string_get_cstr(strbuf));

    // Bottom-right: offset
    furi_string_printf(strbuf, "%+ld", app->ppm_offset);
    canvas_draw_str_aligned(canvas, 127, 62, AlignRight, AlignBottom, furi_string_get_cstr(strbuf));

    // --- Graph line (no threshold lines) ---
    int32_t x_step_num = 127;
    int32_t x_step_den = h->count - 1;

    for(uint16_t i = 0; i < h->count - 1; i++) {
        int32_t x1 = (int32_t)((int64_t)i * x_step_num / x_step_den);
        int32_t x2 = (int32_t)((int64_t)(i + 1) * x_step_num / x_step_den);
        int32_t y1 =
            GRAPH_BOTTOM - (int32_t)((int64_t)(h->data[i] - y_min) * GRAPH_HEIGHT / range);
        int32_t y2 =
            GRAPH_BOTTOM - (int32_t)((int64_t)(h->data[i + 1] - y_min) * GRAPH_HEIGHT / range);

        if(y1 < GRAPH_TOP) y1 = GRAPH_TOP;
        if(y1 > GRAPH_BOTTOM) y1 = GRAPH_BOTTOM;
        if(y2 < GRAPH_TOP) y2 = GRAPH_TOP;
        if(y2 > GRAPH_BOTTOM) y2 = GRAPH_BOTTOM;

        canvas_draw_line(canvas, x1, y1, x2, y2);
    }

    furi_string_free(strbuf);
}

void mh_z19app_draw_callback(Canvas* canvas, void* ctx) {
    furi_assert(ctx);
    MHZ19App* app = ctx;
    canvas_clear(canvas);

    if(!app->have_5v) {
        canvas_set_font(canvas, FontPrimary);
        elements_multiline_text_aligned(
            canvas,
            4,
            28,
            AlignLeft,
            AlignTop,
            "5V on GPIO must be\nenabled, or USB must\nbe connected.");
        return;
    }

    switch(app->current_page) {
    case PAGE_CONNECT:
        draw_page_connect(canvas);
        break;
    case PAGE_CALIBRATE:
        draw_page_calibrate(canvas, app);
        break;
    case PAGE_MEASURE:
        draw_page_measure(canvas, app);
        break;
    case PAGE_DEBUG:
        draw_page_debug(canvas, app);
        break;
    case PAGE_GRAPH:
        draw_page_graph(canvas, app);
        break;
    default:
        draw_page_measure(canvas, app);
        break;
    }
}

// --- Input callback ---

void mh_z19app_input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(ctx);
    FuriMessageQueue* event_queue = ctx;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

// --- Alloc / Free / Init ---

MHZ19App* mh_z19app_alloc() {
    MHZ19App* app = (MHZ19App*)malloc(sizeof(MHZ19App));

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!app->mutex) {
        FURI_LOG_E("MH-Z19", "cannot create mutex\r\n");
        free(app);
        return NULL;
    }
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    app->view_port = view_port_alloc();
    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    view_port_draw_callback_set(app->view_port, mh_z19app_draw_callback, app);
    view_port_input_callback_set(app->view_port, mh_z19app_input_callback, app->event_queue);

    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    uint8_t attempts = 0;
    while(!furi_hal_power_is_otg_enabled() && attempts++ < 5) {
        furi_hal_power_enable_otg();
        furi_delay_ms(10);
    }

    if(furi_hal_power_is_otg_enabled() || furi_hal_power_is_charging()) {
        app->have_5v = true;
    } else {
        app->have_5v = false;
    }

    app->input_pin = &gpio_ext_pa6;
    furi_hal_gpio_init(app->input_pin, GpioModeInput, GpioPullUp, GpioSpeedVeryHigh);

    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    furi_mutex_release(app->mutex);

    return app;
}

void mh_z19app_free(MHZ19App* app) {
    furi_assert(app);

    if(furi_hal_power_is_otg_enabled()) {
        furi_hal_power_disable_otg();
    }

    view_port_enabled_set(app->view_port, false);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);

    furi_message_queue_free(app->event_queue);

    furi_hal_light_set(LightRed | LightGreen | LightBlue, 0x00);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    furi_mutex_free(app->mutex);
    free(app);
}

// --- PWM calculation ---

int32_t calculate_ppm(
    int32_t* prevVal,
    int32_t val,
    int32_t* th,
    int32_t* tl,
    int32_t* h,
    int32_t* l) {
    int32_t tt = furi_get_tick();
    if(val == 1) {
        if(val != *prevVal) {
            *h = tt;
            *tl = *h - *l;
            *prevVal = val;
        }
    } else {
        if(val != *prevVal) {
            *l = tt;
            *th = *l - *h;
            *prevVal = val;
            return SENSOR_RANGE * (*th - 2) / (*th + *tl - 4);
        }
    }
    return -1;
}

// --- Filter functions ---

static void ppm_filter_init(PpmFilter* f) {
    memset(f, 0, sizeof(PpmFilter));
    f->display_status = GreenStatus;
}

static bool ppm_is_valid(int32_t ppm) {
    return (ppm >= 100 && ppm <= SENSOR_RANGE);
}

static void ppm_buffer_push(PpmFilter* f, int32_t ppm) {
    f->buffer[f->head] = ppm;
    f->head = (f->head + 1) & (PPM_BUFFER_SIZE - 1);
    if(f->count < PPM_BUFFER_SIZE) {
        f->count++;
    }
}

static int32_t ppm_buffer_median(const PpmFilter* f) {
    int32_t tmp[PPM_BUFFER_SIZE];
    uint8_t n = f->count;
    for(uint8_t i = 0; i < n; i++) {
        tmp[i] = f->buffer[i];
    }
    for(uint8_t i = 1; i < n; i++) {
        int32_t key = tmp[i];
        int8_t j = i - 1;
        while(j >= 0 && tmp[j] > key) {
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = key;
    }
    if(n % 2 == 1) {
        return tmp[n / 2];
    }
    return (tmp[n / 2 - 1] + tmp[n / 2]) / 2;
}

static int32_t ppm_ema_update(PpmFilter* f, int32_t median_ppm) {
    if(f->ema == 0) {
        f->ema = median_ppm * EMA_SCALE;
    } else {
        f->ema = f->ema + EMA_ALPHA * (median_ppm - f->ema / EMA_SCALE);
    }
    return f->ema / EMA_SCALE;
}

static StatusPPM ppm_compute_status(const PpmFilter* f, int32_t ppm) {
    StatusPPM current = f->display_status;
    switch(current) {
    case GreenStatus:
        if(ppm > 1000 + STATUS_HYST) return RedStatus;
        if(ppm > 800 + STATUS_HYST) return YellowStatus;
        return GreenStatus;
    case YellowStatus:
        if(ppm > 1000 + STATUS_HYST) return RedStatus;
        if(ppm < 800 - STATUS_HYST) return GreenStatus;
        return YellowStatus;
    case RedStatus:
        if(ppm < 800 - STATUS_HYST) return GreenStatus;
        if(ppm < 1000 - STATUS_HYST) return YellowStatus;
        return RedStatus;
    default:
        return GreenStatus;
    }
}

static void ppm_filter_process(PpmFilter* f, int32_t raw_ppm) {
    if(!ppm_is_valid(raw_ppm)) {
        return;
    }
    ppm_buffer_push(f, raw_ppm);
    int32_t median = ppm_buffer_median(f);
    int32_t smoothed = ppm_ema_update(f, median);
    f->display_ppm = smoothed;
    f->display_status = ppm_compute_status(f, f->display_ppm);
}

// --- Init ---

void mh_z19app_init(MHZ19App* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->co2_ppm = 0;
    app->status_ppm = GreenStatus;
    app->current_page = PAGE_CONNECT;
    app->ppm_offset = PPM_OFFSET_DEFAULT;
    app->have_5v = true;
    ppm_filter_init(&app->filter);
    history_init(&app->history);
    notification_message(app->notifications, &green_led_sequence);
    furi_mutex_release(app->mutex);
}

// --- Main ---

int32_t mh_z19_app(void* p) {
    UNUSED(p);
    MHZ19App* app = mh_z19app_alloc();
    if(!app) {
        FURI_LOG_E("MH-Z19", "cannot create app\r\n");
        return -1;
    }

    mh_z19app_init(app);

    InputEvent event;
    int32_t prevVal = 0;
    int32_t th, tl, h = 0;
    int32_t l = 0;
    int32_t ppm = 0;

    for(bool processing = true; processing;) {
        // Fast GPIO polling: 100 reads at 1ms intervals (~100ms block)
        for(int i = 0; i < 100 && processing; i++) {
            ppm = calculate_ppm(
                &prevVal,
                furi_hal_gpio_read(app->input_pin),
                &th,
                &tl,
                &h,
                &l);
            if(ppm > 0) {
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                app->raw_ppm = ppm;
                app->dbg_th = th;
                app->dbg_tl = tl;
                app->read_count++;
                ppm_filter_process(&app->filter, ppm);
                int32_t adjusted = app->filter.display_ppm + app->ppm_offset;
                if(adjusted < 0) adjusted = 0;
                app->co2_ppm = adjusted;
                history_push(&app->history, app->co2_ppm);
                furi_mutex_release(app->mutex);
            }
            furi_delay_ms(1);
        }

        // Update UI and check input every ~100ms
        furi_mutex_acquire(app->mutex, FuriWaitForever);

        // Status with hysteresis on offset-adjusted value
        if(app->co2_ppm > 0) {
            StatusPPM new_status = app->status_ppm;
            if(app->co2_ppm > 1000 + STATUS_HYST) {
                new_status = RedStatus;
            } else if(app->co2_ppm > 800 + STATUS_HYST) {
                if(app->status_ppm == GreenStatus) new_status = YellowStatus;
                else if(app->status_ppm == RedStatus && app->co2_ppm < 1000 - STATUS_HYST)
                    new_status = YellowStatus;
            } else if(app->co2_ppm < 800 - STATUS_HYST) {
                new_status = GreenStatus;
            }
            if(app->status_ppm != new_status) {
                app->status_ppm = new_status;
                switch(app->status_ppm) {
                case GreenStatus:
                    notification_message(app->notifications, &green_led_sequence);
                    break;
                case YellowStatus:
                    notification_message(app->notifications, &yellow_led_sequence);
                    break;
                case RedStatus:
                    notification_message(app->notifications, &red_led_sequence);
                    break;
                }
            }
        }

        if(furi_message_queue_get(app->event_queue, &event, 0) == FuriStatusOk) {
            if(event.type == InputTypeShort) {
                switch(event.key) {
                case InputKeyBack:
                    processing = false;
                    break;
                case InputKeyOk:
                    if(app->current_page < PAGE_MEASURE) {
                        app->current_page++;
                    }
                    break;
                case InputKeyUp:
                    if(app->current_page == PAGE_MEASURE) {
                        app->current_page = PAGE_DEBUG;
                    }
                    break;
                case InputKeyDown:
                    if(app->current_page == PAGE_DEBUG) {
                        app->current_page = PAGE_MEASURE;
                    }
                    break;
                case InputKeyLeft:
                    if(app->current_page == PAGE_CALIBRATE) {
                        if(app->ppm_offset > PPM_OFFSET_MIN) {
                            app->ppm_offset -= PPM_OFFSET_STEP;
                        }
                    } else if(app->current_page == PAGE_GRAPH) {
                        app->current_page = PAGE_MEASURE;
                    }
                    break;
                case InputKeyRight:
                    if(app->current_page == PAGE_CALIBRATE) {
                        if(app->ppm_offset < PPM_OFFSET_MAX) {
                            app->ppm_offset += PPM_OFFSET_STEP;
                        }
                    } else if(app->current_page == PAGE_MEASURE) {
                        app->current_page = PAGE_GRAPH;
                    }
                    break;
                default:
                    break;
                }
            }
        }
        furi_mutex_release(app->mutex);
        view_port_update(app->view_port);
    }

    mh_z19app_free(app);
    return 0;
}
