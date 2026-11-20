# Джерела ресурсів

Карта побудована з меж адміністративних одиниць OpenStreetMap і Wambacher, які публікує geoBoundaries як **UKR-ADM1-14850775**, ADM1, 2017. На екрані відображаються 24 області, АР Крим, Київ і Севастополь. Ліцензія даних — [ODbL 1.0](https://opendatacommons.org/licenses/odbl/1-0/). [Дані GeoJSON](https://github.com/wmgeolab/geoBoundaries/raw/9469f09/releaseData/gbOpen/UKR/ADM1/geoBoundaries-UKR-ADM1_simplified.geojson) · [Атрибуція OpenStreetMap](https://www.openstreetmap.org/copyright)

Збережіть файл GeoJSON за посиланням і виконайте `python main/assets/generate_map.py <geojson> main/assets/ukraine_map_data.h` з кореня проєкту. Генератор перевіряє коди 27 одиниць, наявність усіх регіонів на растрі та розміщення мітки Дніпра. GitHub може повернути LFS pointer: у такому разі завантажте raw-файл через `media.githubusercontent.com/media/` для того самого commit/path.

Фіксовані довідкові координати великих міст у `CITIES` проєктуються тим самим перетворенням, що й межі. Територіальні мітки загроз прив’язано до найближчого внутрішнього пікселя до центру регіону. Це умовне місце значка області, а не координати повітряної цілі. Масштабування використовує наявний растр 424×222; вищий zoom не додає деталізації меж.

Прошивка під час збірки вбудовує Ubuntu Medium з пакета LVGL. Шрифт поширюється за [Ubuntu Font Licence](https://ubuntu.com/legal/font-licence).

Головний екран теми S.T.A.L.K.E.R. використовує Ubuntu Mono Regular із [офіційного архіву Ubuntu Font Family 0.83](https://assets.ubuntu.com/v1/0cef8205-ubuntu-font-family-0.83.zip). Файл `UbuntuMono-R.ttf` з архіву вбудовується без модифікацій під назвою `UbuntuMono-Regular.ttf`. [Ліцензія](UbuntuMono-LICENCE.txt).
