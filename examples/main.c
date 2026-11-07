/*
 * WeAct 2.9" 4-color e-paper, Pico W web UI.
 *
 * Boot order: Wi-Fi first, then the HTTP server, then the display. The panel
 * takes about 25 s per refresh, so nothing that the web UI depends on waits for
 * it. During a refresh the driver calls epd_idle_hook(), which keeps lwIP
 * serviced, so the page stays reachable.
 *
 * Requires the pico_cyw43_arch_lwip_poll library (cyw43_arch_poll() is called
 * from the main loop and from epd_idle_hook()).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/tcp.h"

#include "epaper_display.h"
#include "font.h"

#define PIN_MOSI 11
#define PIN_SCK 10
#define PIN_CS 9
#define PIN_DC 8
#define PIN_RES 12
#define PIN_BUSY 13

#define HTTP_PORT 80
#define REQUEST_SIZE 4096
#define TEXT_SIZE 320 /* bytes; the form allows 150 characters, UTF-8 needs more */
#define MAX_CONNECTIONS 3
#define HTTP_POLL_INTERVAL 4 /* tcp_poll units of 500 ms: every 2 s */
#define HTTP_IDLE_POLLS 10   /* close a connection after ~20 s without progress */
#define MESSAGE_BODY_SIZE 640

#define WIFI_CONNECT_TIMEOUT_MS 15000
#define IP_WAIT_TIMEOUT_MS 15000
#define WIFI_SCAN_TIMEOUT_MS 8000
#define LINK_CHECK_INTERVAL_US 5000000ULL
#define LINK_BAD_CHECKS_BEFORE_RECONNECT 3
#define HEARTBEAT_INTERVAL_US 10000000ULL

/* The panel needs rest between refreshes (vendors recommend >= 180 s). */
#ifndef RENDER_MIN_INTERVAL_S
#define RENDER_MIN_INTERVAL_S 180
#endif

#ifndef EPAPER_HOSTNAME
#define EPAPER_HOSTNAME "epaper"
#endif

#ifndef DISPLAY_TEXT
#define DISPLAY_TEXT ""
#endif

#ifndef DISPLAY_BG
#define DISPLAY_BG EPD_COLOR_WHITE
#endif

#ifndef DISPLAY_ORIENTATION
#define DISPLAY_ORIENTATION 0
#endif

#ifndef WIFI_SSID_2
#define WIFI_SSID_2 ""
#define WIFI_PASSWORD_2 ""
#define WIFI_SSID_3 ""
#define WIFI_PASSWORD_3 ""
#define WIFI_SSID_4 ""
#define WIFI_PASSWORD_4 ""
#endif

typedef struct
{
    char text[TEXT_SIZE];
    uint8_t text_color;
    uint8_t background;
    uint8_t size;
    bool landscape;
} render_params_t;

typedef struct
{
    struct tcp_pcb *pcb;
    char request[REQUEST_SIZE];
    size_t request_length;
    char header[192];
    size_t header_length;
    const char *body;
    size_t body_length;
    char message_body[MESSAGE_BODY_SIZE];
    size_t sent; /* bytes of header + body handed to lwIP so far */
    uint8_t idle_polls;
    bool responding;
} http_conn_t;

static epd_ctx_t display;
static bool display_ready;
static bool display_init_failed;
static bool wifi_ready;

static volatile bool render_pending;
static volatile bool rendering;
static render_params_t pending;
static render_params_t shown;
static bool have_shown;
static uint64_t last_render_us; /* 0 until the first web-requested refresh */

static int connection_count;

typedef struct
{
    const char *ssid;
    const char *password;
} wifi_network_t;

static const wifi_network_t wifi_networks[] = {
    {WIFI_SSID, WIFI_PASSWORD},
    {WIFI_SSID_2, WIFI_PASSWORD_2},
    {WIFI_SSID_3, WIFI_PASSWORD_3},
    {WIFI_SSID_4, WIFI_PASSWORD_4},
};

static const char page[] =
    "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>WeAct E-Paper</title><style>"
    "*{box-sizing:border-box}body{margin:0;background:#f5f1e8;color:#171717;font-family:Arial,sans-serif}"
    "main{max-width:980px;margin:0 auto;padding:28px 22px 48px}header{display:flex;justify-content:space-between;align-items:end;border-bottom:2px solid #171717;padding-bottom:14px;margin-bottom:34px}"
    "h1{font:700 30px Georgia,serif;letter-spacing:.02em;margin:0}header span{font-size:12px;text-transform:uppercase;letter-spacing:.14em;color:#d84b35}"
    ".layout{display:grid;grid-template-columns:minmax(0,1fr) minmax(240px,360px);gap:44px;align-items:start}.controls{max-width:480px}"
    "label{display:block;margin:18px 0 7px;font-size:12px;text-transform:uppercase;letter-spacing:.1em}"
    "textarea,select{width:100%;font:inherit;border:1px solid #171717;border-radius:0;padding:11px;background:#fffdf8;color:#171717}textarea{height:150px;resize:vertical}"
    ".preview-label{font-size:12px;text-transform:uppercase;letter-spacing:.1em;margin:0 0 10px}.preview-area{display:flex;justify-content:center;background:#e8dfd1;padding:20px;min-height:300px}"
    ".screen{width:100%;max-width:300px;aspect-ratio:168/384;border:8px solid #171717;background:#fff;display:flex;align-items:center;justify-content:center;overflow:hidden;transition:aspect-ratio .2s}.screen.landscape{aspect-ratio:384/168;max-width:100%}"
    ".screen-text{white-space:pre-wrap;overflow-wrap:anywhere;text-align:center;line-height:1.15;font-family:monospace;font-weight:700;max-width:96%;word-break:break-word}"
    "button{margin-top:24px;width:100%;padding:13px;border:0;border-radius:0;background:#d84b35;color:#fff;font:700 14px Arial,sans-serif;text-transform:uppercase;letter-spacing:.1em;cursor:pointer}button:hover{background:#b93725}"
    "small{display:block;margin-top:14px;color:#665f56;line-height:1.5}@media(max-width:700px){.layout{grid-template-columns:1fr;gap:30px}header{align-items:start;flex-direction:column;gap:8px}.preview-area{order:-1}}"
    "</style></head><body><main><header><h1>WeAct E-Paper</h1><span>Studio UMZU / prototype</span></header><div class=layout><section class=controls><form method=post action=/render accept-charset=utf-8>"
    "<label for=text>Text</label><textarea id=text name=text maxlength=150 placeholder='Type something...'></textarea>"
    "<label for=orientation>Orientation</label><select id=orientation name=orientation>"
    "<option value=0 selected>Portrait</option><option value=1>Landscape</option></select>"
    "<label for=color>Text color</label><select id=color name=color>"
    "<option value=0>Black</option><option value=2>Yellow</option><option value=3>Red</option></select>"
    "<label for=size>Text size</label><select id=size name=size>"
    "<option value=1>Small</option><option value=2 selected>Medium</option><option value=3>Large</option><option value=4>Extra large</option></select>"
    "<label for=bg>Background</label><select id=bg name=bg>"
    "<option value=1 selected>White</option><option value=0>Black</option><option value=2>Yellow</option><option value=3>Red</option></select>"
    "<button type=submit>Flash display</button></form><small>Preview locally, then send a full refresh to the panel.</small></section>"
    "<section><p class=preview-label>Preview</p><div class=preview-area><div id=screen class=screen><span id=screen-text></span></div></div></section></div></main>"
    "<script>const palette=['#171717','#fffdf8','#e7c84b','#d84b35'];"
    "const text=document.getElementById('text'),color=document.getElementById('color'),size=document.getElementById('size'),bg=document.getElementById('bg'),orientation=document.getElementById('orientation'),screen=document.getElementById('screen'),screenText=document.getElementById('screen-text');"
    "function update(){const landscape=orientation.value==='1';screen.classList.toggle('landscape',landscape);screen.style.background=palette[bg.value];screenText.style.color=palette[color.value];screenText.style.fontSize=(Number(size.value)*5)+'px';screenText.textContent=text.value||'Preview';}"
    "[text,color,size,bg,orientation].forEach(x=>x.addEventListener('input',update));update();</script></body></html>";

/*----------------------------------------------------------------------------*/
/* Failure and diagnostics                                                     */

static void fatal(const char *message) __attribute__((noreturn));

static void fatal(const char *message)
{
    for (;;)
    {
        printf("FATAL: %s\n", message);
        for (int i = 0; i < 6; i++)
        {
            if (wifi_ready)
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, i & 1);
            sleep_ms(100);
        }
        sleep_ms(1500);
    }
}

/*----------------------------------------------------------------------------*/
/* Form parsing                                                                */

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* Decode 'src_len' bytes of application/x-www-form-urlencoded data. */
static void url_decode(char *dst, size_t dst_size, const char *src, size_t src_len)
{
    size_t out = 0;
    size_t i = 0;
    while (i < src_len && out + 1 < dst_size)
    {
        const char c = src[i];
        if (c == '+')
        {
            dst[out++] = ' ';
            i++;
        }
        else if (c == '%' && i + 2 < src_len && hex_value(src[i + 1]) >= 0 &&
                 hex_value(src[i + 2]) >= 0)
        {
            dst[out++] = (char)((hex_value(src[i + 1]) << 4) | hex_value(src[i + 2]));
            i += 3;
        }
        else
        {
            dst[out++] = c;
            i++;
        }
    }
    dst[out] = '\0';
}

/*
 * Find 'name' in a urlencoded body (matching whole field names only) and
 * decode its value.
 */
static bool form_value(const char *body, const char *name, char *value, size_t value_size)
{
    const size_t name_length = strlen(name);
    const char *field = body;
    while (*field)
    {
        const char *end = strchr(field, '&');
        if (!end)
            end = field + strlen(field);
        if ((size_t)(end - field) > name_length && strncmp(field, name, name_length) == 0 &&
            field[name_length] == '=')
        {
            const char *start = field + name_length + 1;
            url_decode(value, value_size, start, (size_t)(end - start));
            return true;
        }
        field = *end ? end + 1 : end;
    }
    return false;
}

/* Cut off a UTF-8 sequence that was truncated by the size limit. */
static void trim_partial_utf8(char *s)
{
    const size_t length = strlen(s);
    size_t i = length;
    while (i > 0 && ((uint8_t)s[i - 1] & 0xC0) == 0x80)
        i--;
    if (i == 0)
        return;
    const uint8_t lead = (uint8_t)s[i - 1];
    const size_t needed = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3
                                         : lead >= 0xC0   ? 2
                                                          : 1;
    if (needed > length - (i - 1))
        s[i - 1] = '\0';
}

/* Missing field: use the fallback. Present but not a number <= max: invalid. */
static bool read_choice(const char *body,
                        const char *name,
                        unsigned fallback,
                        unsigned max,
                        unsigned *out)
{
    char value[16];
    if (!form_value(body, name, value, sizeof(value)))
    {
        *out = fallback;
        return true;
    }
    char *end;
    const unsigned long number = strtoul(value, &end, 10);
    if (end == value || *end != '\0' || number > max)
        return false;
    *out = (unsigned)number;
    return true;
}

static bool params_equal(const render_params_t *a, const render_params_t *b)
{
    return a->text_color == b->text_color && a->background == b->background &&
           a->size == b->size && a->landscape == b->landscape &&
           strcmp(a->text, b->text) == 0;
}

/*----------------------------------------------------------------------------*/
/* Rendering                                                                   */

static void draw_oriented_pixel(uint16_t x, uint16_t y, uint8_t color, bool landscape)
{
    if (landscape)
        epd_draw_pixel(&display, display.width - 1 - y, x, color);
    else
        epd_draw_pixel(&display, x, y, color);
}

static void draw_scaled_text(const char *text,
                             uint8_t color,
                             uint8_t background,
                             uint8_t size,
                             bool landscape)
{
    if (size < 1)
        size = 1;
    if (size > 8)
        size = 8;
    epd_clear(&display, background);

    uint16_t line_count = 1;
    for (const char *p = text; *p; p++)
        if (*p == '\n')
            line_count++;

    const uint16_t canvas_width = landscape ? display.height : display.width;
    const uint16_t canvas_height = landscape ? display.width : display.height;
    const uint16_t line_height = FONT_HEIGHT * size;
    const uint16_t first_y = line_count * line_height < canvas_height
                                 ? (canvas_height - line_count * line_height) / 2
                                 : 0;

    uint16_t line = 0;
    const char *line_start = text;
    while (line_start && *line_start)
    {
        const char *line_end = strchr(line_start, '\n');
        size_t byte_length =
            line_end ? (size_t)(line_end - line_start) : strlen(line_start);
        if (byte_length > 0 && line_start[byte_length - 1] == '\r')
            byte_length--;

        const size_t glyph_count = font_utf8_count(line_start, byte_length);
        const uint32_t line_width = glyph_count * FONT_ADVANCE * size;
        uint16_t x =
            line_width < canvas_width ? (canvas_width - line_width) / 2 : 0;

        const char *p = line_start;
        const char *stop = line_start + byte_length;
        while (p < stop)
        {
            uint32_t codepoint;
            const size_t consumed = font_utf8_decode(p, &codepoint);
            if (consumed == 0)
                break;
            const uint8_t *glyph = font_get_glyph_cp(codepoint);
            for (uint8_t glyph_x = 0; glyph_x < FONT_WIDTH; glyph_x++)
            {
                for (uint8_t glyph_y = 0; glyph_y < FONT_HEIGHT; glyph_y++)
                {
                    if (!(glyph[glyph_x] & (1u << glyph_y)))
                        continue;
                    for (uint8_t sx = 0; sx < size; sx++)
                        for (uint8_t sy = 0; sy < size; sy++)
                            draw_oriented_pixel(
                                x + glyph_x * size + sx,
                                first_y + line * line_height + glyph_y * size + sy,
                                color,
                                landscape);
                }
            }
            x += FONT_ADVANCE * size;
            p += consumed;
        }
        line++;
        line_start = line_end ? line_end + 1 : NULL;
    }
    epd_flush(&display);
}

static void draw_max_text(const char *text, uint8_t background, bool landscape)
{
    const size_t glyph_count = font_utf8_count(text, strlen(text));
    const uint16_t canvas_width = landscape ? display.height : display.width;
    uint8_t size = 8;
    while (size > 1 && glyph_count * FONT_ADVANCE * size > canvas_width)
        size--;
    const uint8_t color =
        background == EPD_COLOR_BLACK ? EPD_COLOR_WHITE : EPD_COLOR_BLACK;
    draw_scaled_text(text, color, background, size, landscape);
}

/*
 * Initialise the display on first use. A failure is remembered, so the web UI
 * keeps running (and says so) instead of the firmware stopping.
 */
static bool ensure_display(void)
{
    if (display_ready)
        return true;
    if (display_init_failed)
        return false;
    const epd_pin_config_t pins = {.sck = PIN_SCK,
                                   .mosi = PIN_MOSI,
                                   .cs = PIN_CS,
                                   .dc = PIN_DC,
                                   .res = PIN_RES,
                                   .busy = PIN_BUSY};
    display_ready = epd_init(&display, &pins, EPD_MODEL_2IN9);
    if (!display_ready)
    {
        display_init_failed = true;
        printf("WARNING: e-paper init failed (check wiring and pins); "
               "the web UI stays up without a display.\n");
    }
    return display_ready;
}

/* Called by the display driver on every poll of the BUSY pin. */
void epd_idle_hook(void);
void epd_idle_hook(void)
{
    if (wifi_ready)
        cyw43_arch_poll();
}

/* Microseconds until the next refresh is allowed (0 = now). */
static uint64_t render_wait_us(uint64_t now)
{
    if (last_render_us == 0)
        return 0;
    const uint64_t ready = last_render_us + (uint64_t)RENDER_MIN_INTERVAL_S * 1000000ULL;
    return now >= ready ? 0 : ready - now;
}

static void do_render(void)
{
    const render_params_t job = pending; /* a new POST may replace 'pending' meanwhile */
    render_pending = false;
    rendering = true;
    printf("Display: refreshing (%u bytes of text)\n", (unsigned)strlen(job.text));
    draw_scaled_text(job.text, job.text_color, job.background, job.size, job.landscape);
    rendering = false;
    shown = job;
    have_shown = true;
    last_render_us = time_us_64();
    printf("Display: refresh done\n");
}

/*----------------------------------------------------------------------------*/
/* HTTP server                                                                 */

static err_t http_close(http_conn_t *c)
{
    struct tcp_pcb *pcb = c->pcb;
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_err(pcb, NULL);
    tcp_poll(pcb, NULL, 0);
    err_t result = ERR_OK;
    if (tcp_close(pcb) != ERR_OK)
    {
        tcp_abort(pcb);
        result = ERR_ABRT; /* callbacks must report an abort to lwIP */
    }
    free(c);
    if (connection_count > 0)
        connection_count--;
    return result;
}

/*
 * Hand as much of the response to lwIP as fits into its send buffer. Called
 * again from tcp_sent (space freed) and tcp_poll (retry). Closes the
 * connection once everything is queued; lwIP sends the remainder and the FIN.
 */
static err_t http_write_more(http_conn_t *c)
{
    const size_t total = c->header_length + c->body_length;
    while (c->sent < total)
    {
        const char *source;
        size_t available;
        if (c->sent < c->header_length)
        {
            source = c->header + c->sent;
            available = c->header_length - c->sent;
        }
        else
        {
            const size_t offset = c->sent - c->header_length;
            source = c->body + offset;
            available = c->body_length - offset;
        }
        const size_t room = tcp_sndbuf(c->pcb);
        if (room == 0)
            break;
        const size_t chunk = available < room ? available : room;
        const err_t error = tcp_write(c->pcb, source, (u16_t)chunk, TCP_WRITE_FLAG_COPY);
        if (error == ERR_MEM)
            break; /* retry when ACKs free up memory */
        if (error != ERR_OK)
            return http_close(c);
        c->sent += chunk;
    }
    tcp_output(c->pcb);
    if (c->sent >= total)
        return http_close(c);
    return ERR_OK;
}

static err_t http_respond(http_conn_t *c,
                          const char *status,
                          const char *extra_headers,
                          const char *body)
{
    c->responding = true;
    c->body = body;
    c->body_length = strlen(body);
    const int length = snprintf(c->header,
                                sizeof(c->header),
                                "HTTP/1.1 %s\r\n"
                                "Content-Type: text/html; charset=utf-8\r\n"
                                "Content-Length: %u\r\n"
                                "Cache-Control: no-store\r\n"
                                "Connection: close\r\n"
                                "%s\r\n",
                                status,
                                (unsigned)c->body_length,
                                extra_headers);
    c->header_length = length < 0 ? 0 : (size_t)length;
    if (c->header_length >= sizeof(c->header))
        c->header_length = sizeof(c->header) - 1;
    c->sent = 0;
    c->idle_polls = 0;
    return http_write_more(c);
}

static err_t http_respond_message(http_conn_t *c, const char *status, const char *message)
{
    snprintf(c->message_body,
             sizeof(c->message_body),
             "<!doctype html><meta charset=utf-8>"
             "<meta name=viewport content='width=device-width,initial-scale=1'>"
             "<title>WeAct E-Paper</title>"
             "<body style='font-family:Arial,sans-serif;max-width:480px;margin:40px auto;padding:0 20px'>"
             "<p>%s</p><p><a href=/>Back</a></p>",
             message);
    return http_respond(c, status, "", c->message_body);
}

static err_t handle_render(http_conn_t *c, const char *body)
{
    render_params_t job;
    memset(&job, 0, sizeof(job));

    unsigned color, size, background, orientation;
    if (!read_choice(body, "color", 0, 3, &color) ||
        !read_choice(body, "size", 2, 8, &size) || size < 1 ||
        !read_choice(body, "bg", 1, 3, &background) ||
        !read_choice(body, "orientation", 0, 1, &orientation))
        return http_respond_message(c, "400 Bad Request", "Invalid form values.");

    if (!form_value(body, "text", job.text, sizeof(job.text)))
        job.text[0] = '\0';
    trim_partial_utf8(job.text);
    job.text_color = (uint8_t)color;
    job.size = (uint8_t)size;
    job.background = (uint8_t)background;
    job.landscape = orientation != 0;

    if (job.text_color == job.background)
        return http_respond_message(
            c, "400 Bad Request", "Text and background have the same colour.");
    if (!display_ready)
        return http_respond_message(c,
                                    "503 Service Unavailable",
                                    "The e-paper display is not available. "
                                    "Check the wiring and the serial log.");

    if (render_pending ? params_equal(&job, &pending)
                       : (have_shown && params_equal(&job, &shown)))
        return http_respond_message(
            c, "200 OK", "The display already shows this, or is about to.");

    pending = job;
    render_pending = true;

    char message[128];
    const uint64_t wait_us = render_wait_us(time_us_64());
    if (rendering)
        snprintf(message, sizeof(message), "A refresh is running. Yours is queued next.");
    else if (wait_us > 0)
        snprintf(message,
                 sizeof(message),
                 "Queued. The panel rests between refreshes; yours starts in %u s.",
                 (unsigned)((wait_us + 999999) / 1000000));
    else
        snprintf(message,
                 sizeof(message),
                 "Refreshing now. The panel needs about 25 seconds.");
    return http_respond_message(c, "200 OK", message);
}

/* Returns the declared body length, 0 without Content-Length, SIZE_MAX if bad. */
static size_t request_body_length(const char *request, const char *header_end)
{
    static const char name[] = "Content-Length:";
    const size_t name_length = sizeof(name) - 1;
    const char *line = strstr(request, "\r\n"); /* end of the request line */
    while (line && line < header_end)
    {
        line += 2;
        if (strncasecmp(line, name, name_length) == 0)
        {
            char *end;
            const unsigned long number = strtoul(line + name_length, &end, 10);
            if (end == line + name_length || number > REQUEST_SIZE)
                return SIZE_MAX;
            return (size_t)number;
        }
        line = strstr(line, "\r\n");
    }
    return 0;
}

static err_t http_dispatch(http_conn_t *c, size_t header_length, size_t body_length)
{
    char *request = c->request;

    const char *eol = strstr(request, "\r\n");
    int line_length = eol ? (int)(eol - request) : 0;
    if (line_length > 60)
        line_length = 60;
    printf("HTTP %.*s\n", line_length, request);

    if (body_length == SIZE_MAX || header_length + body_length > REQUEST_SIZE - 1)
        return http_respond_message(c, "413 Payload Too Large", "Request too large.");

    if (strncmp(request, "GET / ", 6) == 0 || strncmp(request, "GET /?", 6) == 0)
        return http_respond(c, "200 OK", "", page);
    if (strncmp(request, "GET /favicon.ico ", 17) == 0)
        return http_respond(c, "204 No Content", "", "");
    if (strncmp(request, "POST /render ", 13) == 0)
    {
        request[header_length + body_length] = '\0';
        return handle_render(c, request + header_length);
    }
    return http_respond_message(c, "404 Not Found", "Page not found.");
}

static err_t http_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *packet, err_t error)
{
    http_conn_t *c = (http_conn_t *)arg;
    (void)pcb;
    if (!packet) /* the client closed the connection */
        return http_close(c);
    if (error != ERR_OK)
    {
        pbuf_free(packet);
        return http_close(c);
    }

    c->idle_polls = 0;
    tcp_recved(c->pcb, packet->tot_len);
    if (c->responding)
    {
        pbuf_free(packet);
        return ERR_OK;
    }

    const size_t room = REQUEST_SIZE - 1 - c->request_length;
    const size_t copied = packet->tot_len < room ? packet->tot_len : room;
    pbuf_copy_partial(packet, c->request + c->request_length, (u16_t)copied, 0);
    const bool overflow = packet->tot_len > room;
    pbuf_free(packet);
    c->request_length += copied;
    c->request[c->request_length] = '\0';
    if (overflow)
        return http_respond_message(c, "413 Payload Too Large", "Request too large.");

    const char *header_end = strstr(c->request, "\r\n\r\n");
    if (!header_end)
        return ERR_OK; /* wait for the rest of the headers */
    const size_t header_length = (size_t)(header_end + 4 - c->request);
    const size_t body_length = request_body_length(c->request, header_end);
    if (body_length != SIZE_MAX && c->request_length < header_length + body_length)
        return ERR_OK; /* wait for the rest of the body */
    return http_dispatch(c, header_length, body_length);
}

static err_t http_sent(void *arg, struct tcp_pcb *pcb, u16_t length)
{
    http_conn_t *c = (http_conn_t *)arg;
    (void)pcb;
    (void)length;
    c->idle_polls = 0;
    if (c->responding)
        return http_write_more(c);
    return ERR_OK;
}

static err_t http_poll(void *arg, struct tcp_pcb *pcb)
{
    http_conn_t *c = (http_conn_t *)arg;
    (void)pcb;
    if (++c->idle_polls >= HTTP_IDLE_POLLS)
    {
        printf("HTTP: closing idle connection\n");
        return http_close(c);
    }
    if (c->responding)
        return http_write_more(c); /* retry a write that hit ERR_MEM */
    return ERR_OK;
}

static void http_err(void *arg, err_t error)
{
    /* The pcb is already freed by lwIP; only release our state. */
    http_conn_t *c = (http_conn_t *)arg;
    (void)error;
    if (c)
    {
        free(c);
        if (connection_count > 0)
            connection_count--;
    }
}

static err_t http_accept(void *arg, struct tcp_pcb *client, err_t error)
{
    (void)arg;
    if (error != ERR_OK || !client)
        return ERR_VAL;
    if (connection_count >= MAX_CONNECTIONS)
    {
        printf("HTTP: too many connections, rejecting\n");
        return ERR_MEM; /* lwIP aborts the new connection */
    }
    http_conn_t *c = (http_conn_t *)calloc(1, sizeof(*c));
    if (!c)
        return ERR_MEM;
    c->pcb = client;
    connection_count++;
    tcp_arg(client, c);
    tcp_recv(client, http_recv);
    tcp_sent(client, http_sent);
    tcp_err(client, http_err);
    tcp_poll(client, http_poll, HTTP_POLL_INTERVAL);
    return ERR_OK;
}

static bool start_server(void)
{
    struct tcp_pcb *server = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!server || tcp_bind(server, IP_ANY_TYPE, HTTP_PORT) != ERR_OK)
        return false;
    server = tcp_listen(server);
    if (!server)
        return false;
    tcp_accept(server, http_accept);
    return true;
}

/*----------------------------------------------------------------------------*/
/* Wi-Fi                                                                       */

static const ip4_addr_t *sta_ip(void)
{
    return netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]);
}

static void sta_ip_string(char *buffer, size_t size)
{
    ip4addr_ntoa_r(sta_ip(), buffer, (int)size);
}

static bool wait_for_ip(uint32_t timeout_ms)
{
    const absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (ip4_addr_isany_val(*sta_ip()))
    {
        if (time_reached(deadline))
            return false;
        cyw43_arch_poll();
        sleep_ms(20);
    }
    return true;
}

static size_t configured_network_count(void)
{
    size_t count = 0;
    for (size_t i = 0; i < sizeof(wifi_networks) / sizeof(wifi_networks[0]); i++)
        if (wifi_networks[i].ssid[0])
            count++;
    return count;
}

/*
 * Security modes to try, most common first. Routers differ (WPA2, WPA2+WPA3,
 * old WPA/WPA2 with TKIP, WPA3 only), and a wrong guess shows up as a failed
 * join even with the right password.
 */
typedef struct
{
    uint32_t auth;
    const char *name;
} wifi_auth_mode_t;

static const wifi_auth_mode_t wifi_auth_modes[] = {
    {CYW43_AUTH_WPA3_WPA2_AES_PSK, "WPA3/WPA2"},
    {CYW43_AUTH_WPA2_AES_PSK, "WPA2"},
    {CYW43_AUTH_WPA2_MIXED_PSK, "WPA2 mixed (TKIP)"},
    {CYW43_AUTH_WPA3_SAE_AES_PSK, "WPA3"},
};
#define WIFI_AUTH_MODE_COUNT (sizeof(wifi_auth_modes) / sizeof(wifi_auth_modes[0]))

/* What the last round learned about one network; shown if joining fails. */
typedef struct
{
    const char *ssid;
    size_t password_length;
    bool scanned; /* the scan itself worked */
    bool seen;    /* the SSID was in the scan results */
    uint8_t channel;
    int16_t rssi;
    uint8_t auth_mode;
    size_t attempts;
    int codes[WIFI_AUTH_MODE_COUNT];
} wifi_report_t;

static wifi_report_t wifi_report; /* first configured network, latest round */

/* Static: the driver may still call back after we stop waiting. */
static struct
{
    const char *ssid;
    size_t ssid_length;
    bool seen;
    uint8_t channel;
    int16_t rssi;
    uint8_t auth_mode;
    unsigned total;
} wifi_scan;

static int wifi_scan_result(void *env, const cyw43_ev_scan_result_t *result)
{
    (void)env;
    if (!result)
        return 0;
    wifi_scan.total++;
    if (result->ssid_len == wifi_scan.ssid_length &&
        memcmp(result->ssid, wifi_scan.ssid, wifi_scan.ssid_length) == 0 &&
        (!wifi_scan.seen || result->rssi > wifi_scan.rssi))
    {
        wifi_scan.seen = true;
        wifi_scan.channel = (uint8_t)result->channel;
        wifi_scan.rssi = (int16_t)result->rssi;
        wifi_scan.auth_mode = (uint8_t)result->auth_mode;
    }
    return 0;
}

/* Scan for the SSID. Returns false if the scan itself could not run. */
static bool wifi_scan_for(const char *ssid)
{
    memset(&wifi_scan, 0, sizeof(wifi_scan));
    wifi_scan.ssid = ssid;
    wifi_scan.ssid_length = strlen(ssid);

    cyw43_wifi_scan_options_t options;
    memset(&options, 0, sizeof(options));
    const int error = cyw43_wifi_scan(&cyw43_state, &options, NULL, wifi_scan_result);
    if (error)
    {
        printf("Wi-Fi: scan failed (%d)\n", error);
        return false;
    }
    const absolute_time_t deadline = make_timeout_time_ms(WIFI_SCAN_TIMEOUT_MS);
    while (cyw43_wifi_scan_active(&cyw43_state) && !time_reached(deadline))
    {
        cyw43_arch_poll();
        sleep_ms(50);
    }
    if (wifi_scan.seen)
        printf("Wi-Fi: scan saw %u networks; \"%s\" on channel %u, %d dBm, security flags %u\n",
               wifi_scan.total,
               ssid,
               (unsigned)wifi_scan.channel,
               (int)wifi_scan.rssi,
               (unsigned)wifi_scan.auth_mode);
    else
        printf("Wi-Fi: scan saw %u networks, but not \"%s\" "
               "(2.4 GHz only; check the name, range and channel)\n",
               wifi_scan.total,
               ssid);
    return true;
}

static bool wifi_try_network(const wifi_network_t *network, wifi_report_t *report)
{
    const bool open = network->password[0] == '\0';
    memset(report, 0, sizeof(*report));
    report->ssid = network->ssid;
    report->password_length = strlen(network->password);
    printf("Wi-Fi: \"%s\" (password length %u)\n",
           network->ssid,
           (unsigned)report->password_length);

    report->scanned = wifi_scan_for(network->ssid);
    if (report->scanned && wifi_scan.seen)
    {
        report->seen = true;
        report->channel = wifi_scan.channel;
        report->rssi = wifi_scan.rssi;
        report->auth_mode = wifi_scan.auth_mode;
    }

    const size_t mode_count = open ? 1 : WIFI_AUTH_MODE_COUNT;
    for (size_t i = 0; i < mode_count; i++)
    {
        const uint32_t auth = open ? CYW43_AUTH_OPEN : wifi_auth_modes[i].auth;
        const char *name = open ? "open" : wifi_auth_modes[i].name;
        printf("Wi-Fi:   trying %s...\n", name);
        const int result = cyw43_arch_wifi_connect_timeout_ms(
            network->ssid, network->password, auth, WIFI_CONNECT_TIMEOUT_MS);
        report->codes[i] = result;
        report->attempts = i + 1;
        if (result == 0)
        {
            printf("Wi-Fi: joined \"%s\" using %s\n", network->ssid, name);
            return true;
        }
        printf("Wi-Fi:   %s failed (%d)\n", name, result);
        if (report->scanned && !report->seen)
            break; /* not in range: other security modes cannot help */
    }
    return false;
}

/* One pass over all configured networks. Fills 'first_report' for the first. */
static bool wifi_try_round(wifi_report_t *first_report)
{
    bool first = true;
    for (size_t i = 0; i < sizeof(wifi_networks) / sizeof(wifi_networks[0]); i++)
    {
        if (!wifi_networks[i].ssid[0])
            continue;
        wifi_report_t scratch;
        wifi_report_t *report = first ? first_report : &scratch;
        first = false;
        if (wifi_try_network(&wifi_networks[i], report))
            return true;
    }
    return false;
}

/* Text for the panel: enough to tell why the join failed without a serial cable. */
static void wifi_failure_text(const wifi_report_t *report,
                              size_t other_networks,
                              char *out,
                              size_t size)
{
    char seen[40];
    if (!report->scanned)
        snprintf(seen, sizeof(seen), "scan failed");
    else if (!report->seen)
        snprintf(seen, sizeof(seen), "NOT SEEN (2.4 GHz?)");
    else
        snprintf(seen, sizeof(seen), "ch%u %ddBm sec%u",
                 (unsigned)report->channel, (int)report->rssi, (unsigned)report->auth_mode);

    char codes[48] = "";
    size_t used = 0;
    for (size_t i = 0; i < report->attempts && used + 8 < sizeof(codes); i++)
        used += (size_t)snprintf(codes + used, sizeof(codes) - used, i ? " %d" : "%d",
                                 report->codes[i]);

    int length = snprintf(out, size, "No Wi-Fi\n%.26s\npw length %u\n%s\nerr %s",
                          report->ssid ? report->ssid : "?",
                          (unsigned)report->password_length, seen, codes);
    if (other_networks > 0 && length > 0 && (size_t)length < size)
        snprintf(out + length, size - (size_t)length, "\n+%u more", (unsigned)other_networks);
}

/*
 * Show a short message on the panel, once per boot, so a headless device tells
 * you why it is not reachable.
 */
static void show_boot_status_once(const char *message)
{
    static bool shown_once;
    if (shown_once || !ensure_display())
        return;
    shown_once = true;
    draw_scaled_text(message, EPD_COLOR_BLACK, EPD_COLOR_WHITE, 1, false);
}

static void connect_to_wifi(bool at_boot)
{
    if (configured_network_count() == 0)
        fatal("no Wi-Fi network configured (set WIFI_SSID in .env)");

    while (true)
    {
        if (wifi_try_round(&wifi_report))
        {
#ifdef CYW43_PERFORMANCE_PM
            /* Default power saving makes a server slow to answer. */
            cyw43_wifi_pm(&cyw43_state, CYW43_PERFORMANCE_PM);
#endif
            return;
        }
        printf("Wi-Fi: no network connected. Common causes: wrong password, "
               "5 GHz-only network, weak signal. Retrying...\n");
        if (at_boot)
        {
            char text[200];
            wifi_failure_text(&wifi_report, configured_network_count() - 1, text, sizeof(text));
            show_boot_status_once(text);
        }
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        sleep_ms(1000);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    }
}

/*----------------------------------------------------------------------------*/

int main(void)
{
    stdio_init_all();
    printf("\nWeAct e-paper web UI starting\n");

    /* The country code matters: the default one hides channels 12 and 13. */
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_GERMANY))
        fatal("Wi-Fi chip init failed");
    cyw43_arch_enable_sta_mode();
    wifi_ready = true;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
#if LWIP_NETIF_HOSTNAME
    netif_set_hostname(&cyw43_state.netif[CYW43_ITF_STA], EPAPER_HOSTNAME);
#endif
    connect_to_wifi(true);
    if (!wait_for_ip(IP_WAIT_TIMEOUT_MS))
        printf("Wi-Fi: joined, but no IP address yet (DHCP slow?)\n");
    char ip_text[IP4ADDR_STRLEN_MAX];
    sta_ip_string(ip_text, sizeof(ip_text));

    if (!start_server())
        fatal("HTTP server failed to start");
    printf("Web UI is up: http://%s/\n", ip_text);

    /* Only now the slow part. The server keeps answering during refreshes. */
    if (ensure_display())
    {
        if (DISPLAY_TEXT[0])
            draw_max_text(DISPLAY_TEXT, DISPLAY_BG, DISPLAY_ORIENTATION != 0);
        else
        {
            char ip_message[64];
            snprintf(ip_message, sizeof(ip_message), "Web UI:\n%s", ip_text);
            draw_scaled_text(ip_message, EPD_COLOR_BLACK, EPD_COLOR_WHITE, 1, false);
        }
    }

    uint64_t now = time_us_64();
    uint64_t next_led_toggle = now + 500000;
    uint64_t next_link_check = now + LINK_CHECK_INTERVAL_US;
    uint64_t next_heartbeat = now + HEARTBEAT_INTERVAL_US;
    bool led_state = true;
    int bad_link_checks = 0;
    while (true)
    {
        cyw43_arch_poll();
        now = time_us_64();

        if (render_pending && display_ready && render_wait_us(now) == 0)
            do_render();

        if (now >= next_led_toggle)
        {
            led_state = !led_state;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
            next_led_toggle = now + 500000;
        }

        if (now >= next_link_check)
        {
            next_link_check = now + LINK_CHECK_INTERVAL_US;
            if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP)
                bad_link_checks = 0;
            else if (++bad_link_checks >= LINK_BAD_CHECKS_BEFORE_RECONNECT)
            {
                printf("Wi-Fi: link lost, reconnecting\n");
                connect_to_wifi(false);
                wait_for_ip(IP_WAIT_TIMEOUT_MS);
                bad_link_checks = 0;
            }
        }

        if (now >= next_heartbeat)
        {
            next_heartbeat = now + HEARTBEAT_INTERVAL_US;
            sta_ip_string(ip_text, sizeof(ip_text));
            printf("alive: ip=%s link=%d connections=%d display=%s pending=%d\n",
                   ip_text,
                   cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA),
                   connection_count,
                   display_ready ? "ok" : "none",
                   render_pending ? 1 : 0);
        }

        sleep_ms(10);
    }
}