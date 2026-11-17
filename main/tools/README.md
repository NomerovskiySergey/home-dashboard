# Ключі джерел даних

1. Під’єднайте ESP32 з останньою прошивкою й закрийте серійний монітор. Відкрийте PowerShell у `C:\work\home-dashboard`.
2. Для графіка ЦЕК 5.2 запустіть:

```powershell
& 'C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe' main/tools/set_alerts_token.py --service energy --port COM3
```

Для карти повітряних тривог використовуйте той самий скрипт із `--service alerts`. Підставте фактичний COM-порт. Для карти ключ можна [запросити в Alerts.in.ua](https://alerts.in.ua/api-request).

3. У прихованому запиті введіть лише ключ обраного сервісу. Після успішного запису перезапустіть плату.

Ключі записуються в окремі розділи `energy_keys` і `dash_keys`. Кожен передається лише своєму сервісу в Bearer-заголовку HTTPS. Для графіка це [маршрут області](https://alerts.energy/developers/api/operators/source-registry-areas-slug-shutdowns), із якого плата вибирає ЦЕК 5.2. Тимчасові файли прибираються після запису. Ключі зберігаються у флеш-пам’яті плати без шифрування.
