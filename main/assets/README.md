# Джерела ресурсів

Карта побудована з меж адміністративних одиниць OpenStreetMap і Wambacher, які публікує geoBoundaries як **UKR-ADM1-14850775**, ADM1, 2017. На екрані відображаються 24 області, АР Крим, Київ і Севастополь. Ліцензія даних — [ODbL 1.0](https://opendatacommons.org/licenses/odbl/1-0/). [Дані GeoJSON](https://github.com/wmgeolab/geoBoundaries/raw/9469f09/releaseData/gbOpen/UKR/ADM1/geoBoundaries-UKR-ADM1_simplified.geojson) · [Атрибуція OpenStreetMap](https://www.openstreetmap.org/copyright)

Збережіть файл GeoJSON за посиланням і виконайте `python main/assets/generate_map.py <geojson> main/assets/ukraine_map_data.h` з кореня проєкту. Генератор перевіряє коди 27 одиниць, наявність усіх регіонів на растрі та розміщення мітки Дніпра.

Прошивка під час збірки вбудовує Ubuntu Medium з пакета LVGL. Шрифт поширюється за [Ubuntu Font Licence](https://ubuntu.com/legal/font-licence).
