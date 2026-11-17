#include "dashboard_wifi_web.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "network_provisioning/manager.h"

static esp_netif_t *setup_netif;
static const char *setup_code;

static bool from_setup_network(httpd_req_t *request)
{
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(setup_netif, &info) != ESP_OK || !info.ip.addr) {
        return false;
    }

    struct sockaddr_storage local;
    socklen_t length = sizeof(local);
    if (getsockname(httpd_req_to_sockfd(request), (struct sockaddr *)&local, &length) != 0) {
        return false;
    }
    if (local.ss_family == AF_INET) {
        const struct sockaddr_in *address = (const struct sockaddr_in *)&local;
        return address->sin_addr.s_addr == info.ip.addr;
    }
#if CONFIG_LWIP_IPV6
    if (local.ss_family == AF_INET6) {
        const struct sockaddr_in6 *address = (const struct sockaddr_in6 *)&local;
        static const uint8_t mapped_ipv4_prefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255};
        const uint8_t *ip = address->sin6_addr.s6_addr;
        return memcmp(ip, mapped_ipv4_prefix, sizeof(mapped_ipv4_prefix)) == 0 &&
               memcmp(ip + sizeof(mapped_ipv4_prefix), &info.ip.addr, sizeof(info.ip.addr)) == 0;
    }
#endif
    return false;
}

static void page_headers(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Content-Security-Policy",
                      "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'");
}

static esp_err_t show_form(httpd_req_t *request)
{
    if (!from_setup_network(request)) {
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Connect to the setup network.");
    }
    char page[1300];
    const int length = snprintf(
        page, sizeof(page),
        "<!doctype html><html lang=uk><meta charset=utf-8>"
        "<meta name=viewport content=\"width=device-width, initial-scale=1\">"
        "<title>Wi-Fi — Home Dashboard</title>"
        "<style>body{max-width:30rem;margin:2rem auto;padding:0 1rem;font:1rem system-ui}"
        "label,input,button{display:block}input,button{box-sizing:border-box;width:100%%;"
        "padding:.65rem;font:inherit;margin:.25rem 0 1rem}</style>"
        "<h1>Налаштування Wi-Fi</h1><p>Введіть назву домашньої мережі 2,4 ГГц.</p>"
        "<form method=post action=/wifi><input type=hidden name=code value=\"%s\">"
        "<label for=ssid>Назва мережі</label>"
        "<input id=ssid name=ssid required maxlength=32 autocomplete=off autocapitalize=none>"
        "<label for=pass>Пароль мережі</label>"
        "<input id=pass name=pass type=password maxlength=64 autocomplete=off>"
        "<button type=submit>Підключити</button></form></html>", setup_code);
    if (length < 0 || (size_t)length >= sizeof(page)) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    page_headers(request);
    return httpd_resp_send(request, page, length);
}

static int hex_digit(unsigned char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    character = tolower(character);
    return character >= 'a' && character <= 'f' ? character - 'a' + 10 : -1;
}

static bool decode_field(char *text)
{
    char *out = text;
    for (const char *in = text; *in; ++in) {
        if (*in == '%') {
            if (!in[1] || !in[2]) {
                return false;
            }
            const int high = hex_digit((unsigned char)in[1]);
            const int low = hex_digit((unsigned char)in[2]);
            if (high < 0 || low < 0 || (high == 0 && low == 0)) {
                return false;
            }
            *out++ = (char)(high * 16 + low);
            in += 2;
        } else {
            *out++ = *in == '+' ? ' ' : *in;
        }
    }
    *out = '\0';
    return true;
}

static bool form_field(const char *body, const char *key, char *field, size_t capacity)
{
    return httpd_query_key_value(body, key, field, capacity) == ESP_OK && decode_field(field);
}

static bool valid_password(const char *pass, size_t length)
{
    if (!length || (length >= 8 && length <= 63)) {
        return true;
    }
    return length == 64 && strspn(pass, "0123456789abcdefABCDEF") == length;
}

static esp_err_t submit_form(httpd_req_t *request)
{
    if (!from_setup_network(request)) {
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Connect to the setup network.");
    }

    char body[512];
    if (!request->content_len || request->content_len >= sizeof(body)) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid Wi-Fi form.");
    }
    size_t received = 0;
    while (received < request->content_len) {
        const int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) {
            return httpd_resp_send_err(request, HTTPD_408_REQ_TIMEOUT, "Form input timed out.");
        }
        received += (size_t)count;
    }
    body[received] = '\0';

    char code[49], ssid[97], pass[193];
    if (!form_field(body, "code", code, sizeof(code)) || strcmp(code, setup_code) != 0) {
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Open the setup page again.");
    }
    if (!form_field(body, "ssid", ssid, sizeof(ssid)) ||
        !form_field(body, "pass", pass, sizeof(pass))) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid Wi-Fi form.");
    }

    wifi_config_t wifi = {0};
    const size_t ssid_length = strlen(ssid);
    const size_t pass_length = strlen(pass);
    if (!ssid_length || ssid_length > sizeof(wifi.sta.ssid) || !valid_password(pass, pass_length)) {
        page_headers(request);
        return httpd_resp_sendstr(request,
            "<meta charset=utf-8><p>Перевірте назву мережі та пароль.</p><a href=/>Спробувати ще раз</a>");
    }
    memcpy(wifi.sta.ssid, ssid, ssid_length);
    memcpy(wifi.sta.password, pass, pass_length);

    const esp_err_t submitted = network_prov_mgr_configure_wifi_sta(&wifi);
    memset(&wifi, 0, sizeof(wifi));
    memset(body, 0, sizeof(body));
    memset(pass, 0, sizeof(pass));
    if (submitted != ESP_OK) {
        page_headers(request);
        return httpd_resp_sendstr(request,
            "<meta charset=utf-8><p>Зачекайте кілька секунд і перевірте екран плати.</p>"
            "<a href=/>Повернутися до форми</a>");
    }

    page_headers(request);
    return httpd_resp_sendstr(request,
        "<meta charset=utf-8><meta name=viewport content=\"width=device-width, initial-scale=1\">"
        "<p>Плата підключається. Результат з’явиться на екрані.</p>"
        "<a href=/>Повернутися до форми</a>");
}

esp_err_t dashboard_wifi_web_start(esp_netif_t *access_point, const char *code,
                                   httpd_handle_t *server)
{
    setup_netif = access_point;
    setup_code = code;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    esp_err_t result = httpd_start(server, &config);
    if (result != ESP_OK) {
        return result;
    }
    const httpd_uri_t form = { .uri = "/", .method = HTTP_GET, .handler = show_form };
    const httpd_uri_t submit = { .uri = "/wifi", .method = HTTP_POST, .handler = submit_form };
    result = httpd_register_uri_handler(*server, &form);
    if (result == ESP_OK) {
        result = httpd_register_uri_handler(*server, &submit);
    }
    if (result != ESP_OK) {
        httpd_stop(*server);
        *server = NULL;
    }
    return result;
}
