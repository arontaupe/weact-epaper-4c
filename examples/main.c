#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#define TEXT_SIZE 160

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

static epd_ctx_t display;
static volatile bool render_pending;
static char pending_text[TEXT_SIZE];
static uint8_t pending_text_color;
static uint8_t pending_background_color;
static uint8_t pending_text_size;
static bool pending_landscape;

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
    "<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
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
    "</style></head><body><main><header><h1>WeAct E-Paper</h1><span>Studio UMZU / prototype</span></header><div class=layout><section class=controls><form method=post action=/render>"
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

static void url_decode(char *dst, size_t dst_size, const char *src)
{
    size_t out = 0;
    while (*src && out + 1 < dst_size)
    {
        if (*src == '+')
        {
            dst[out++] = ' ';
            src++;
        }
        else if (*src == '%' && src[1] && src[2] && hex_value(src[1]) >= 0 && hex_value(src[2]) >= 0)
        {
            dst[out++] = (char)((hex_value(src[1]) << 4) | hex_value(src[2]));
            src += 3;
        }
        else
        {
            dst[out++] = *src++;
        }
    }
    dst[out] = '\0';
}

static bool form_value(const char *body, const char *name, char *value, size_t value_size)
{
    char key[24];
    snprintf(key, sizeof(key), "%s=", name);
    const char *start = strstr(body, key);
    if (!start)
        return false;
    start += strlen(key);
    const char *end = strchr(start, '&');
    size_t length = end ? (size_t)(end - start) : strlen(start);
    if (length >= value_size)
        length = value_size - 1;
    char encoded[TEXT_SIZE];
    if (length >= sizeof(encoded))
        length = sizeof(encoded) - 1;
    memcpy(encoded, start, length);
    encoded[length] = '\0';
    url_decode(value, value_size, encoded);
    return true;
}

static void draw_oriented_pixel(uint16_t x, uint16_t y, uint8_t color, bool landscape)
{
    if (landscape)
        epd_draw_pixel(&display, display.width - 1 - y, x, color);
    else
        epd_draw_pixel(&display, x, y, color);
}

static void draw_scaled_text(const char *text, uint8_t color, uint8_t background, uint8_t size, bool landscape)
{
    if (size < 1)
        size = 1;
    if (size > 8)
        size = 8;
    epd_clear(&display, background);

    uint16_t line_count = 1;
    uint16_t longest = 0;
    uint16_t current = 0;
    for (const char *p = text; *p; p++)
    {
        if (*p == '\n')
        {
            if (current > longest)
                longest = current;
            current = 0;
            line_count++;
        }
        else
        {
            current++;
        }
    }
    if (current > longest)
        longest = current;
    (void)longest;

    uint16_t canvas_width = landscape ? display.height : display.width;
    uint16_t canvas_height = landscape ? display.width : display.height;
    uint16_t line_height = 8 * size;
    uint16_t first_y = line_count * line_height < canvas_height
                           ? (canvas_height - line_count * line_height) / 2
                           : 0;
    uint16_t line = 0;
    const char *line_start = text;
    while (line_start && *line_start)
    {
        const char *line_end = strchr(line_start, '\n');
        size_t length = line_end ? (size_t)(line_end - line_start) : strlen(line_start);
        uint32_t line_width = length * 6 * size;
        uint16_t x = line_width < canvas_width ? (canvas_width - line_width) / 2 : 0;
        for (size_t index = 0; index < length; index++)
        {
            const uint8_t *glyph = font_get_glyph(line_start[index]);
            for (uint8_t glyph_x = 0; glyph_x < FONT_WIDTH; glyph_x++)
            {
                for (uint8_t glyph_y = 0; glyph_y < FONT_HEIGHT; glyph_y++)
                {
                    if (!(glyph[glyph_x] & (1 << glyph_y)))
                        continue;
                    for (uint8_t sx = 0; sx < size; sx++)
                        for (uint8_t sy = 0; sy < size; sy++)
                            draw_oriented_pixel(x + glyph_x * size + sx,
                                                first_y + line * line_height + glyph_y * size + sy,
                                                color, landscape);
                }
            }
            x += 6 * size;
        }
        line++;
        line_start = line_end ? line_end + 1 : NULL;
    }
    epd_flush(&display);
}

static void draw_max_text(const char *text, uint8_t background, bool landscape)
{
    size_t length = strlen(text);
    uint16_t canvas_width = landscape ? display.height : display.width;
    uint8_t size = 8;
    while (size > 1 && length * 6 * size > canvas_width)
        size--;
    uint8_t color = background == EPD_COLOR_BLACK ? EPD_COLOR_WHITE : EPD_COLOR_BLACK;
    draw_scaled_text(text, color, background, size, landscape);
}

static void send_response(struct tcp_pcb *client, const char *status, const char *body)
{
    char header[160];
    int length = snprintf(header, sizeof(header),
                          "HTTP/1.1 %s\r\nContent-Type: text/html\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
                          status, (unsigned)strlen(body));
    tcp_write(client, header, (u16_t)length, TCP_WRITE_FLAG_COPY);
    tcp_write(client, body, (u16_t)strlen(body), TCP_WRITE_FLAG_COPY);
    tcp_output(client);
    tcp_close(client);
}

static void handle_request(struct tcp_pcb *client, char *request)
{
    if (strncmp(request, "GET / ", 6) == 0 || strncmp(request, "GET /?", 6) == 0)
    {
        send_response(client, "200 OK", page);
        return;
    }
    if (strncmp(request, "POST /render ", 13) == 0)
    {
        char *body = strstr(request, "\r\n\r\n");
        char text[TEXT_SIZE] = "";
        char value[16];
        if (body)
        {
            body += 4;
            form_value(body, "text", text, sizeof(text));
            pending_text_color = (uint8_t)strtoul(form_value(body, "color", value, sizeof(value)) ? value : "0", NULL, 10);
            pending_text_size = (uint8_t)strtoul(form_value(body, "size", value, sizeof(value)) ? value : "2", NULL, 10);
            pending_background_color = (uint8_t)strtoul(form_value(body, "bg", value, sizeof(value)) ? value : "1", NULL, 10);
            pending_landscape = strtoul(form_value(body, "orientation", value, sizeof(value)) ? value : "0", NULL, 10) != 0;
            strncpy(pending_text, text, sizeof(pending_text) - 1);
            pending_text[sizeof(pending_text) - 1] = '\0';
            render_pending = true;
        }
        send_response(client, "303 See Other\r\nLocation: /", "");
        return;
    }
    send_response(client, "404 Not Found", "<h1>Not found</h1>");
}

static size_t request_body_length(const char *request)
{
    const char *header_end = strstr(request, "\r\n\r\n");
    if (!header_end)
        return SIZE_MAX;
    const char *content_length = strstr(request, "Content-Length:");
    if (!content_length || content_length > header_end)
        return 0;
    return strtoul(content_length + strlen("Content-Length:"), NULL, 10);
}

static err_t http_recv(void *arg, struct tcp_pcb *client, struct pbuf *packet, err_t error)
{
    static char request[REQUEST_SIZE];
    static size_t request_length;
    (void)arg;
    if (error != ERR_OK || !packet)
    {
        if (packet)
            pbuf_free(packet);
        tcp_close(client);
        request_length = 0;
        return ERR_OK;
    }
    tcp_recved(client, packet->tot_len);
    size_t available = REQUEST_SIZE - 1 - request_length;
    size_t copied = packet->tot_len < available ? packet->tot_len : available;
    pbuf_copy_partial(packet, request + request_length, (u16_t)copied, 0);
    request_length += copied;
    request[request_length] = '\0';
    pbuf_free(packet);
    const char *header_end = strstr(request, "\r\n\r\n");
    if (header_end)
    {
        size_t header_length = (size_t)(header_end + 4 - request);
        size_t body_length = request_body_length(request);
        if (body_length != SIZE_MAX && request_length >= header_length + body_length)
        {
            handle_request(client, request);
            request_length = 0;
        }
    }
    return ERR_OK;
}

static err_t http_accept(void *arg, struct tcp_pcb *client, err_t error)
{
    (void)arg;
    if (error != ERR_OK)
        return error;
    tcp_recv(client, http_recv);
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

static void connect_to_wifi(void)
{
    bool try_mixed_auth = true;
    while (true)
    {
        for (size_t network = 0; network < sizeof(wifi_networks) / sizeof(wifi_networks[0]); network++)
        {
            if (!wifi_networks[network].ssid[0])
                continue;
            uint32_t auth = wifi_networks[network].password[0]
                                ? (try_mixed_auth ? CYW43_AUTH_WPA3_WPA2_AES_PSK : CYW43_AUTH_WPA2_AES_PSK)
                                : CYW43_AUTH_OPEN;
            printf("Trying Wi-Fi network %u (%s)...\n", (unsigned)(network + 1), wifi_networks[network].ssid);
            int result = cyw43_arch_wifi_connect_timeout_ms(
                wifi_networks[network].ssid, wifi_networks[network].password, auth, 15000);
            if (result == 0)
                return;
            printf("Wi-Fi network %u failed (%d).\n", (unsigned)(network + 1), result);
        }
        printf("No configured Wi-Fi network connected; retrying all networks...\n");
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        sleep_ms(1000);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        try_mixed_auth = !try_mixed_auth;
    }
}

int main(void)
{
    stdio_init_all();
    const epd_pin_config_t pins = {.sck = PIN_SCK, .mosi = PIN_MOSI, .cs = PIN_CS, .dc = PIN_DC, .res = PIN_RES, .busy = PIN_BUSY};
    if (!epd_init(&display, &pins, EPD_MODEL_2IN9))
        return 1;
    epd_clear(&display, EPD_COLOR_WHITE);
    epd_draw_str(&display, 10, 10, "Connecting Wi-Fi", EPD_COLOR_BLACK);
    epd_flush(&display);

    if (cyw43_arch_init())
        return 1;
    cyw43_arch_enable_sta_mode();
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    printf("Connecting to Wi-Fi...\n");
    connect_to_wifi();
    const char *ip_address = ip4addr_ntoa(netif_ip4_addr(netif_default));
    printf("Wi-Fi connected. Open http://%s/\n", ip_address);
    if (DISPLAY_TEXT[0])
        draw_max_text(DISPLAY_TEXT, DISPLAY_BG, DISPLAY_ORIENTATION != 0);
    else
    {
        char ip_message[64];
        snprintf(ip_message, sizeof(ip_message), "Web UI:\n%s", ip_address);
        draw_scaled_text(ip_message, EPD_COLOR_BLACK, EPD_COLOR_WHITE, 1, false);
    }
    if (!start_server())
        return 1;

    uint64_t next_led_toggle = time_us_64() + 500000;
    bool led_state = true;
    while (true)
    {
        if (render_pending)
        {
            render_pending = false;
            draw_scaled_text(pending_text, pending_text_color, pending_background_color,
                             pending_text_size, pending_landscape);
        }
        cyw43_arch_poll();
        if (time_us_64() >= next_led_toggle)
        {
            led_state = !led_state;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
            next_led_toggle = time_us_64() + 500000;
        }
        sleep_ms(10);
    }
}
