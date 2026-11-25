# Довідник модулів, структур і публічного API

Проєкт написано на C. Аналог документації класів тут — контракти модулів, typedef struct, enum та функції. [Схема типів Archify](archify/types.html) показує основний граф структур; нижче наведено також типи, які не винесені на схему.

## Точки входу й координація

| Модуль | Контракт і відповідальність |
| --- | --- |
| [dashboard.c](../main/dashboard.c) | app_main: порядок запуску TZ, LCD/touch/LVGL, Wi-Fi, data та UI; static set_backlight керує LEDC |
| [dashboard_screen.h](../main/dashboard_screen.h) | dashboard_screen_create(touch, set_backlight): створення UI; виклик під LVGL lock. Реалізація володіє навігацією, callbacks і таймерами |
| [dashboard_data.h](../main/dashboard_data.h) | Міст між UI й workers: старт, копії стану, зміна selection, каталог, recovery |

### dashboard_data

| Функція | Семантика |
| --- | --- |
| dashboard_data_start | Завантажує selection/keys та створює supervisor і три workers. Повторний виклик після початку startup повертає invalid state |
| dashboard_data_snapshot(out) | Копіює latest і обчислює актуальність за віком, Wi-Fi, recovery та поколіннями |
| dashboard_data_alert_details(out, expected) | Копіює details; bool означає сумісність з expected. При false не трактувати копію як актуальні details |
| dashboard_data_select_location(city) | UI caller; перевіряє pointer із каталогу, зберігає NVS, потім змінює generation та очищує стару погоду |
| dashboard_data_select_energy(selection) | UI caller; валідує та зберігає всю трійку, потім публікує generation й очищує графік |
| dashboard_data_energy_catalog_request(kind, area, initiator) | Повертає generation запиту; 0 означає invalid/OOM або неготовий mutex. HTTPS виконає energy worker |
| dashboard_data_energy_catalog_copy(generation, out, state) | bool означає збіг покоління й наявність shared каталогу; out заповнюється лише за FRESH. out може бути NULL для перевірки state |
| dashboard_data_energy_catalog_cancel | Інвалідує generation, скидає pending і звільняє shared каталог; не закриває чужий активний HTTP client |
| dashboard_data_restart | Неблокувально запитує recovery всієї групи. false: запит не прийнятий, наприклад startup не готовий, pending або cooldown |
| dashboard_data_recovery(out) | Копіює summary recovery під lock |

Внутрішні data_worker_t, http_body_t та enum джерел приватні для dashboard_data.c. data_worker_t описує сервіс і Bearer; http_body_t — буфер, межі, HTTP headers та cancelled/overflow. Клієнт і буфер належать worker; їх не потрібно зберігати чи звільняти в UI.

## Каталог міста

[dashboard_location.h](../main/dashboard_location.h), [реалізація](../main/dashboard_location.c).

| API | Поведінка |
| --- | --- |
| dashboard_city_count / at / find / default | Незмінний вбудований каталог; at/find можуть повернути NULL, default — Дніпро |
| dashboard_region_count / at / find | Каталог територій із ISO-кодом та окремим alerts_uid |
| dashboard_city_weather_url | Формує Open-Meteo URL за координатами; bool сигналізує валідність аргументів і місткість буфера |
| dashboard_location_load | Повертає відоме місто з NVS або default |
| dashboard_location_save | Приймає справжній елемент каталогу, записує ID і commit; esp_err_t передає помилку |

Повернуті const pointers посилаються на статичний каталог; caller їх не звільняє. Координати latitude_e4/longitude_e4 — градуси × 10000, map_x/map_y — координати в проєкції вбудованої карти.

## Відключення та каталоги

[dashboard_energy.h](../main/dashboard_energy.h), [реалізація](../main/dashboard_energy.c), [parser](../main/dashboard_energy_parse.c).

| API | Поведінка |
| --- | --- |
| dashboard_energy_id_valid | Перевіряє bounded NUL-terminated ID; queue є display string, а не URL-компонентом |
| dashboard_energy_selection_valid | Перевіряє area/initiator/queue і назви |
| dashboard_energy_load(out) | Завжди встановлює fallback; true лише за валідного збереженого selection |
| dashboard_energy_save | Один JSON із усіма значеннями та commit; caller публікує стан лише після ESP_OK |
| dashboard_energy_catalog_parse | Bounded parsing AREAS/SUPPLIERS/QUEUES. false вимагає відкинути output, навіть якщо там є часткові записи |
| dashboard_energy_area_hint | Підказка початкової області за КАТОТТГ; не застосовує новий selection |
| dashboard_parse_energy_selected | Парсить графік конкретного initiator та queue |
| dashboard_parse_energy | Обгортка парсингу типового графіка |

## Парсери, JSON і тривоги

[dashboard_data_parse.h](../main/dashboard_data_parse.h): dashboard_parse_weather перетворює валідну температуру °C в int десятих градуса; dashboard_parse_alerts перевіряє набір R/Y/P/N і відображає його в ISO-indexed array. Caller зобов'язаний перевірити bool перед публікацією.

[dashboard_alert_details.h](../main/dashboard_alert_details.h) групує такі API:

| Функції | Призначення |
| --- | --- |
| dashboard_alert_details_parse / parse_region | Парсинг default/обраної території, локальних records і загальноукраїнських масок загроз |
| dashboard_alert_visit / uid_is / is_local | Обхід bounded JSON records та допоміжні перевірки ідентифікації; visitor отримує cJSON item лише під час callback |
| dashboard_alert_copy_text | Обмежене копіювання тексту з урахуванням UTF-8 |
| dashboard_alert_parse_utc / elapsed | Час початку з API та тривалість |
| dashboard_alert_kyiv_clock / kyiv_datetime | Форматування київського часу/дати |
| dashboard_alert_threat_name / primary | Назва типу загрози та вибір придатного основного запису; primary pointer належить переданій структурі |
| dashboard_alert_cache_update | Валідація HTTP 200/304 та оновлення Last-Modified cache |
| dashboard_alert_rate_take / rate_pause | Спільний бюджет запитів і сервісні паузи |
| dashboard_alert_details_current | Перевірка відповідності статусу й часу details карті |

[dashboard_json.h](../main/dashboard_json.h): inline dashboard_json_object_end знаходить кінець одного object до побудови cJSON, обмежує вкладеність 32 та запис 32768 байт. Невідомі вкладені поля теж проходять цю перевірку. Спеціалізовані парсери можуть мати додаткові менші межі.

## Recovery, екран і теми

| Модуль | Публічні функції та контекст |
| --- | --- |
| [dashboard_recovery.h](../main/dashboard_recovery.h) | init, request, heartbeat, poll, begin, finish: чиста lifecycle-модель. group_request/update/poll: узгодження групи. Caller серіалізує доступ; модель не створює задач і не закриває HTTP |
| [dashboard_display_mode.h](../main/dashboard_display_mode.h) | is_night, brightness, idle, touch, alert: чисті рішення щодо нічного режиму, ECO та wake. bool touch означає поглинання дотику; bool alert — факт пробудження |
| [dashboard_theme.h](../main/dashboard_theme.h) | dashboard_theme_catalog/count та dashboard_theme_find(id): const theme descriptors із day/night palettes та home_layout |
| [dashboard_map_view.h](../main/dashboard_map_view.h) | dashboard_map_project/source: fixed transform туди/назад; categories: bitmask → унікальні категорії icons; group_size: розмір групи. LVGL та HTTP не потрібні |
| [dashboard_pda.h](../main/dashboard_pda.h) | dashboard_pda_draw: декоративне малювання у LVGL layer з переданими кольорами й marker |
| [dashboard_pipboy.h](../main/dashboard_pipboy.h) | dashboard_pipboy_draw: аналогічний draw helper для Fallout layout |

У таблиці recovery/display назви init тощо скорочені: повні символи мають префікси dashboard_recovery_ та dashboard_display_. Це звичайні функції C, не методи структур.

## Wi-Fi

[dashboard_wifi.h](../main/dashboard_wifi.h): start запускає NVS/netif/event loop/Wi-Fi; request_setup публікує подію й не чекає введення користувача; state читає atomic state. connection(out) повертає поточні SSID/IP/RSSI/associated, очищує output за відсутньої асоціації та не читає пароль. setup_name/code повертають рядки, які належать модулю.

[dashboard_wifi_web.h](../main/dashboard_wifi_web.h): dashboard_wifi_web_start(access_point, code, server) запускає локальний HTTP server з GET / і POST /wifi та повертає handle через out-аргумент. Життєвий цикл server координує dashboard_wifi. Код форми зберігає pointer на code, тому його lifetime має охоплювати server.

## Структури даних

| Тип | Ключові поля | Власність / одиниці |
| --- | --- | --- |
| dashboard_data_snapshot_t | location, location_generation, states, updated_us, temperature_tenths, region_alerts, energy, energy_selection, energy_generation, energy_configured | Копія latest; timestamps монотонні μs; city pointer у const каталог |
| dashboard_city_t | id/name, latitude_e4/longitude_e4, region, map_x/map_y | Статичний каталог, caller не звільняє |
| dashboard_region_t | code, alerts_uid, name | Окремі ISO та API UID |
| dashboard_energy_selection_t | area[80], initiator[80], area_name[112], supplier_name[112], queue[24] | Значення застосованої конфігурації або UI draft |
| dashboard_energy_entry_t | id[80], name[112], katottg[24] | Один запис каталогу |
| dashboard_energy_catalog_t | count, entries[64] | Велика bounded структура; runtime copies у PSRAM |
| dashboard_energy_schedule_t | today, tomorrow | Дві day-структури за значенням |
| dashboard_energy_day_t | date[11], state, slots[48] | ISO date, півгодинні слоти CLEAR/POSSIBLE/OFF |
| dashboard_alert_record_t | location[96], message[256], started_at, threats[4], threat_count, omitted, level, oblast, truncated | UTC start у секундах; bounded текст і типи |
| dashboard_alert_details_t | records[8], count/omitted, region_threats[78] | Локальні записи та bitmask за ISO-індексом |
| dashboard_alert_cache_t | modified[80], checked_s, valid | Умовний HTTP-кеш окремого endpoint |
| dashboard_alert_rate_t | requests[6], count, blocked_until | Спільний бюджет у монотонних секундах |
| dashboard_recovery_t | state, pending, automatic, requests/restarts, heartbeat/requested/restarted_us | Caller захищає спільний доступ |
| dashboard_display_mode_t | night/eco/alert_wake flags, asleep, suppress_touch, alert_known/active, last_activity_ms | UI-стан, монотонні ms |
| dashboard_palette_t | colors[DASH_COLOR_COUNT] | RGB-значення за семантичними ролями |
| dashboard_theme_t | id, home_layout, name, radius/border, day/night | Незмінні дескриптори тем |
| dashboard_wifi_connection_t | ssid[33], ipv4[16], rssi_dbm, associated | Поточна асоціація, без credentials |

## Legacy і склад збірки

[dashboard_alert_history.h](../main/dashboard_alert_history.h) зберігає history record/history/request structs та функції parse/minutes/format/open/begin/complete для старої реалізації. Їхній source і тести залишені в репозиторії, але dashboard_alert_history.c не включено у [main/CMakeLists.txt](../main/CMakeLists.txt). Не слід додавати history HTTP, buffers чи екран до runtime-схеми лише тому, що відповідний файл існує.

Сторонні драйвери, LVGL, ESP-IDF та generated Board Manager code не документуються тут як власні модулі проєкту. Залежності фіксуються в idf_component.yml і dependencies.lock; локальні build/generated файли не є окремими фічами.
