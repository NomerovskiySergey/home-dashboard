#pragma once

#include "esp_http_server.h"
#include "esp_netif.h"

esp_err_t dashboard_wifi_web_start(esp_netif_t *access_point, const char *code,
                                   httpd_handle_t *server);
