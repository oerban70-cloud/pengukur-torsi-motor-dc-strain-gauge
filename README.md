# Pengukur Torsi Motor DC — ESP32 + Strain Gauge + HX711 + LCD I2C

Motormu kuat di kertas, tapi torsinya tak pernah terukur. Alat kecil ini
yang mengukurnya: strain gauge BF350-3AA ditempel pada lengan torsi baja
yang menahan rumah motor, dan torsinya tampil langsung di LCD — lengkap
dengan buzzer dan LED yang menyala begitu torsinya melewati 0,50 N.m.

Video penjelasan (60 detik):
link menyusul setelah upload

## Komponen

| Bagian | Keterangan |
|---|---|
| ESP32 DevKit | Otak alat |
| Strain gauge BF350-3AA | 350 ohm, faktor gauge 2,0, regangan maks 2% |
| Resistor 350 ohm (3 buah) | Pelengkap jembatan Wheatstone |
| Modul HX711 | ADC 24-bit, gain 128 kanal A, eksitasi 4,3 V |
| LCD I2C 16x2 (0x27) | Tampil torsi dan torsi tertinggi |
| Tombol push button | Tare (nolkan pembacaan) |
| Buzzer aktif HIGH | Bunyi bila torsi melebihi 0,50 N.m |
| LED merah + resistor 220 Ω | Menyala bersama buzzer saat alarm |
| Motor DC 12 V | Beban yang diukur torsinya |
| Adaptor 12 V 2 A | Catu motor, terpisah dari ESP32 |
| Adaptor 5 V USB 2 A | Catu ESP32, HX711, dan LCD |

## Wiring

| Dari | Ke |
|---|---|
| Strain gauge + 3x 350 ohm | Jembatan Wheatstone: E+/E-/A+/A- ke HX711 |
| HX711 E+ / E- | Jembatan Wheatstone (eksitasi 4,3 V) |
| HX711 A+ / A- | Jembatan Wheatstone (sinyal) |
| HX711 DT | ESP32 GPIO16 |
| HX711 SCK | ESP32 GPIO17 |
| HX711 VCC | ESP32 VIN 5 V |
| HX711 GND | GND |
| LCD SDA | ESP32 GPIO21 |
| LCD SCL | ESP32 GPIO22 |
| LCD VCC | ESP32 VIN 5 V |
| LCD GND | GND |
| Tombol tare | ESP32 GPIO13 (kaki lain ke GND, aktif LOW) |
| Buzzer + | ESP32 GPIO14 |
| Buzzer - | GND |
| LED anoda | ESP32 GPIO26 (katoda lewat resistor 220 Ω ke GND) |
| Motor DC + / - | Adaptor 12 V + / GND |
| Semua GND | Digabung |

Skema lengkap: [schematic.png](schematic.png)

## Cara kerja

Saat motor berputar melawan beban, gaya reaksi memuntir lengan torsi baja
tempat rumah motor dipasang. Strain gauge yang ditempel di lengan itu
meregang sepersekian milimeter, dan resistansinya berubah. Perubahan itu
dibaca jembatan Wheatstone (satu gauge aktif + tiga resistor 350 ohm),
lalu diperkuat dan diubah jadi angka digital oleh HX711 — ADC 24-bit
dengan gain 128 di kanal A.

ESP32 mengubah angka mentah HX711 jadi torsi dalam newton meter (N.m)
memakai faktor kalibrasi:

```text
torsi (N.m) = nilai mentah HX711 / FAKTOR_KALIBRASI
```

Torsi ditampilkan di LCD tiap 0,5 detik bersama torsi tertinggi, dan
dikirim juga ke Serial Monitor. Satu hal yang wajib dilakukan saat pertama
menyalakan alat: motor harus mati dan lengan bebas, karena sketch
menolkan pembacaan sendiri dari 10 sampel pertama.

## Alarm beban berlebih

Satu mekanisme alarm dipakai di alat ini: buzzer dan LED sebagai penanda.
Begitu torsi melewati ambang 0,50 N.m, buzzer (GPIO14) berbunyi dan LED
merah (GPIO26) menyala, dan LCD menampilkan "ALARM! >0.50". Alarm memakai
histeresis — ia mati lagi hanya bila torsi turun di bawah 0,45 N.m —
supaya tidak berkedip di sekitar ambang. Sketch tidak memutus motor —
kurangi beban atau matikan motor secara manual begitu alarm berbunyi.

## Kalibrasi

Ubah `FAKTOR_KALIBRASI` di sketch dengan hasil kalibrasi alatmu sendiri.
Caranya:

1. Upload sketch, buka Serial Monitor (115200 baud).
2. Motor mati dan lengan torsi bebas, tekan tombol tare (atau reset ESP32).
3. Gantung beban yang beratnya diketahui di ujung lengan torsi, mis.
   massa 500 gram pada lengan 10 cm dari poros.
   Torsi beban itu = 0,500 kg x 9,81 m/s2 x 0,10 m = 0,49 N.m.
4. Baca "nilai mentah" yang tampil di Serial Monitor.
5. Hitung: FAKTOR_KALIBRASI = nilai mentah / torsi beban (N.m).
   Contoh: nilai mentah 122500 untuk beban 0,49 N.m
   -> 122500 / 0,49 = 250000.
6. Ganti FAKTOR_KALIBRASI di sketch dengan hasil hitungan, upload ulang,
   lalu uji lagi dengan beban lain untuk memastikan hasilnya akurat.

## Konfigurasi di sketch

| Konstanta | Nilai | Keterangan |
|---|---|---|
| `PIN_HX711_DT` / `PIN_HX711_SCK` | GPIO16 / GPIO17 | Data dan clock HX711 |
| `PIN_LCD_SDA` / `PIN_LCD_SCL` | GPIO21 / GPIO22 | Bus I2C LCD |
| `PIN_TOMBOL_TARE` | GPIO13 | Tombol tare, aktif LOW |
| `PIN_BUZZER` | GPIO14 | Buzzer aktif HIGH |
| `PIN_LED_ALARM` | GPIO26 | LED merah alarm |
| `ALAMAT_LCD_I2C` | 0x27 | Alamat I2C LCD |
| `FAKTOR_KALIBRASI` | 250000,0 | Nilai awal, HARUS dikalibrasi ulang |
| `PANJANG_LENGAN_M` | 0,10 | Lengan torsi kalibrasi: 10 cm |
| `AMBANG_ALARM_NM` | 0,50 | Alarm bila torsi melewati 0,50 N.m |
| `AMBANG_LEPAS_NM` | 0,45 | Alarm mati lagi di bawah 0,45 N.m |
| `INTERVAL_BACA_MS` | 500 | Ukur dan tampil tiap 0,5 detik |

## Angka compile (terverifikasi)

- Board: ESP32 DevKit (esp32:esp32:esp32doit-devkit-v1), core 4.0.0
- Arduino CLI 1.6.0, 26 Nov 2026
- Sketch: 318.129 byte (24%) dari program storage
- RAM: 24.648 byte (7%) dari dynamic memory
- Library: HX711 Arduino Library 0.7.5, LiquidCrystal I2C 1.1.2
  (ada di folder `libraries/`)

## Peringatan

Motor DC 12 V dicatu dari adaptor sendiri. JANGAN catu motor dari pin 5V
ESP32 — arusnya bisa merusak board. Strain gauge BF350-3AA hanya tahan
regangan sampai 2% — jangan tekuk lengan torsi sampai gauge retak.
Kalau angka torsi meleset jauh padahal motor mati, periksa sambungan
jembatan Wheatstone dan ulangi tare (tekan tombol atau nyalakan ulang
alat dengan motor mati).
