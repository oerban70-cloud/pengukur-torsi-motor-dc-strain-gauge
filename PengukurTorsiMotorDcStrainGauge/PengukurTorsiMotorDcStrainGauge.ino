// ============================================================================
//  PengukurTorsiMotorDcStrainGauge.ino — Pengukur Torsi Motor DC
//  (ESP32 + Strain Gauge BF350-3AA + HX711 + LCD I2C)
//  ----------------------------------------------------------------------------
//  Fungsi:
//    - Mengukur torsi (momen puntir) motor DC dari regangan lengan torsi
//    - Menampilkan torsi (N.m) dan torsi tertinggi di LCD I2C 16x2
//    - Tombol tare untuk menolkan pembacaan (mis. setelah motor dipasang)
//    - Buzzer + LED menyala saat torsi melewati ambang (motor kelebihan beban)
//    - Status torsi dikirim ke Serial Monitor
//
//  Cara kerja singkat:
//    Strain gauge BF350-3AA (350 ohm, faktor gauge 2,0) ditempel pada lengan
//    torsi baja yang menahan rumah motor. Saat motor berputar melawan beban,
//    gaya reaksi memuntir lengan itu, gauge meregang, resistansinya berubah
//    sepersekian persen. Perubahan itu dibaca jembatan Wheatstone (gauge +
//    tiga resistor 350 ohm) lalu diperkuat dan diubah jadi angka digital
//    oleh HX711 (ADC 24-bit, gain 128). ESP32 mengubah angka mentah itu
//    jadi torsi dalam newton meter (N.m) memakai faktor kalibrasi.
//
//  Hardware:
//    - ESP32 DevKit (board: esp32:esp32:esp32)
//    - Strain gauge BF350-3AA + 3x resistor 350 ohm (jembatan Wheatstone)
//    - Modul HX711 (hijau, kanal A, gain 128)
//    - LCD I2C 16x2 (alamat 0x27)
//    - Tombol push button (tare)
//    - Buzzer aktif (aktif HIGH)
//    - LED merah + resistor 220 ohm (alarm)
//    - Motor DC 12 V + adaptor 12 V 2 A (beban yang diukur torsinya)
//
//  Pin (TETAP — sesuai skema Fritzing video):
//    HX711 DT  -> GPIO16
//    HX711 SCK -> GPIO17
//    LCD I2C SDA -> GPIO21, SCL -> GPIO22 (I2C default ESP32)
//    Tombol tare -> GPIO13 (INPUT_PULLUP, aktif LOW)
//    Buzzer      -> GPIO14 (aktif HIGH)
//    LED alarm   -> GPIO26 (anoda, katoda lewat resistor 220 ohm ke GND)
// ============================================================================

#include <Wire.h>
#include <HX711.h>
#include <LiquidCrystal_I2C.h>

// ---------------------------------------------------------------------------
//  KONFIGURASI PIN
// ---------------------------------------------------------------------------
const uint8_t PIN_HX711_DT    = 16;  // HX711 DT (data)
const uint8_t PIN_HX711_SCK   = 17;  // HX711 SCK (clock)
const uint8_t PIN_LCD_SDA     = 21;  // LCD I2C SDA
const uint8_t PIN_LCD_SCL     = 22;  // LCD I2C SCL
const uint8_t PIN_TOMBOL_TARE = 13;  // Tombol tare, INPUT_PULLUP, aktif LOW
const uint8_t PIN_BUZZER      = 14;  // Buzzer aktif, aktif HIGH
const uint8_t PIN_LED_ALARM   = 26;  // LED merah alarm, aktif HIGH

// ---------------------------------------------------------------------------
//  KONFIGURASI LCD
// ---------------------------------------------------------------------------
const uint8_t ALAMAT_LCD_I2C = 0x27;  // Alamat I2C modul LCD
const uint8_t LCD_KOLOM      = 16;    // Jumlah kolom LCD
const uint8_t LCD_BARIS      = 2;     // Jumlah baris LCD

// ---------------------------------------------------------------------------
//  KONFIGURASI TORSI
// ---------------------------------------------------------------------------
//  FAKTOR KALIBRASI — NILAI INI HARUS DIKALIBRASI ULANG!
//  Setiap gauge + lengan torsi punya faktor berbeda, jadi angka di bawah ini
//  HANYA nilai awal agar sketch bisa jalan, BUKAN hasil kalibrasi.
//
//  Cara kalibrasi singkat:
//    1. Upload sketch, buka Serial Monitor (115200 baud).
//    2. Motor mati dan lengan torsi bebas (tanpa beban), tekan tombol tare
//       (atau reset ESP32) — ini menolkan pembacaan.
//    3. Gantung beban yang beratnya diketahui di ujung lengan torsi,
//       mis. massa 500 gram pada lengan 10 cm dari poros.
//       Torsi beban itu = 0,500 kg x 9,81 m/s2 x 0,10 m = 0,49 N.m.
//    4. Baca "nilai mentah" yang tampil di Serial Monitor.
//    5. Hitung: FAKTOR_KALIBRASI = nilai mentah / torsi beban (N.m).
//       Contoh: nilai mentah 122500 untuk beban 0,49 N.m
//       -> 122500 / 0,49 = 250000.
//    6. Ganti FAKTOR_KALIBRASI di bawah dengan hasil hitungan, upload ulang,
//       lalu uji lagi dengan beban lain untuk memastikan hasilnya akurat.
const float FAKTOR_KALIBRASI = 250000.0;  // Satuan mentah HX711 per N.m

const float PANJANG_LENGAN_M = 0.10;   // Lengan torsi kalibrasi: 10 cm
const float GRAVITASI_MS2    = 9.81;   // Percepatan gravitasi (m/s2)
const float MASSA_KALIBRASI_KG = 0.50; // Massa beban kalibrasi contoh: 500 g
const float AMBANG_ALARM_NM  = 0.50;   // Alarm bila torsi melewati 0,50 N.m
const float AMBANG_LEPAS_NM  = 0.45;   // Alarm mati lagi di bawah 0,45 N.m
const uint8_t JUMLAH_SAMPEL_BACA = 10; // Rata-rata N sampel tiap pembacaan

// ---------------------------------------------------------------------------
//  KONFIGURASI WAKTU (milisekon)
// ---------------------------------------------------------------------------
const unsigned long INTERVAL_BACA_MS    = 500;   // Jeda antar pembacaan torsi
const unsigned long DEBOUNCE_TOMBOL_MS  = 50;    // Debounce tombol tare
const unsigned long DURASI_BUZZER_MS    = 500;   // Durasi bunyi buzzer alarm
const unsigned long BAUD_SERIAL         = 115200;

// ---------------------------------------------------------------------------
//  OBJEK PERANGKAT
// ---------------------------------------------------------------------------
HX711 timbangan;
LiquidCrystal_I2C lcd(ALAMAT_LCD_I2C, LCD_KOLOM, LCD_BARIS);

// ---------------------------------------------------------------------------
//  STATE
// ---------------------------------------------------------------------------
unsigned long waktuBacaTerakhir   = 0;
unsigned long waktuBuzzerMulai   = 0;
unsigned long waktuTombolTerakhir = 0;
bool buzzerAktif                  = false;
bool alarmAktif                   = false;
bool tombolTareTerakhir           = HIGH;  // HIGH = belum ditekan (pull-up)
float torsiTertinggiNm            = 0.0;

// ---------------------------------------------------------------------------
//  SETUP
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(BAUD_SERIAL);
  Serial.println(F("Pengukur Torsi Motor DC — mulai..."));

  pinMode(PIN_TOMBOL_TARE, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_ALARM, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED_ALARM, LOW);

  // LCD I2C
  Wire.begin(PIN_LCD_SDA, PIN_LCD_SCL);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print(F("Pengukur Torsi"));
  lcd.setCursor(0, 1);
  lcd.print(F("Menyiapkan..."));

  // HX711: kanal A, gain 128 (bawaan begin)
  timbangan.begin(PIN_HX711_DT, PIN_HX711_SCK);
  timbangan.set_scale(FAKTOR_KALIBRASI);
  timbangan.tare(JUMLAH_SAMPEL_BACA);  // Nol otomatis saat dinyalakan

  Serial.println(F("Siap. Tekan tombol tare untuk menolkan."));
  Serial.println(F("Kalibrasi: gantung beban diketahui di lengan 10 cm,"));
  Serial.print(F("contoh beban 500 g = torsi "));
  Serial.print(MASSA_KALIBRASI_KG * GRAVITASI_MS2 * PANJANG_LENGAN_M, 2);
  Serial.println(F(" N.m; faktor = nilai mentah / torsi (N.m)."));
  lcd.clear();
}

// ---------------------------------------------------------------------------
//  LOOP UTAMA
// ---------------------------------------------------------------------------
void loop() {
  unsigned long sekarang = millis();

  // 1. Baca tombol tare (dengan debounce)
  bacaTombolTare(sekarang);

  // 2. Matikan buzzer setelah durasinya habis
  if (buzzerAktif && (sekarang - waktuBuzzerMulai >= DURASI_BUZZER_MS)) {
    digitalWrite(PIN_BUZZER, LOW);
    buzzerAktif = false;
  }

  // 3. Baca torsi secara berkala
  if (sekarang - waktuBacaTerakhir >= INTERVAL_BACA_MS) {
    waktuBacaTerakhir = sekarang;
    float torsiNm = bacaTorsi();
    if (torsiNm > torsiTertinggiNm) {
      torsiTertinggiNm = torsiNm;
    }
    cekAlarm(torsiNm, sekarang);
    tampilkanTorsi(torsiNm);
    kirimStatusSerial(torsiNm);
  }
}

// ---------------------------------------------------------------------------
//  bacaTorsi — baca torsi dari HX711 (N.m), rata-rata N sampel.
//  Hasil negatif kecil (noise di sekitar nol) dibulatkan ke 0.
// ---------------------------------------------------------------------------
float bacaTorsi() {
  float torsi = timbangan.get_units(JUMLAH_SAMPEL_BACA);
  if (torsi < 0.0 && torsi > -0.01) {
    torsi = 0.0;
  }
  return torsi;
}

// ---------------------------------------------------------------------------
//  tampilkanTorsi — tampilkan torsi di LCD 16x2.
//  Baris 1: "Torsi: 0.35 Nm"  |  Baris 2: "Maks 0.50 Nm" / "ALARM! >0.50"
// ---------------------------------------------------------------------------
void tampilkanTorsi(float torsiNm) {
  lcd.setCursor(0, 0);
  lcd.print(F("Torsi: "));
  lcd.print(torsiNm, 2);  // 2 angka desimal: 0.35
  lcd.print(F(" Nm  "));  // Spasi ekstra menimpa sisa angka lama

  lcd.setCursor(0, 1);
  if (alarmAktif) {
    lcd.print(F("ALARM! >0.50   "));
  } else {
    lcd.print(F("Maks "));
    lcd.print(torsiTertinggiNm, 2);
    lcd.print(F(" Nm      "));
  }
}

// ---------------------------------------------------------------------------
//  cekAlarm — nyalakan buzzer + LED saat torsi melewati ambang.
//  Histeresis: alarm mati lagi hanya bila torsi turun di bawah 0,45 N.m,
//  supaya alarm tidak berkedip di sekitar ambang.
// ---------------------------------------------------------------------------
void cekAlarm(float torsiNm, unsigned long sekarang) {
  if (!alarmAktif && torsiNm > AMBANG_ALARM_NM) {
    alarmAktif = true;
    digitalWrite(PIN_LED_ALARM, HIGH);
    digitalWrite(PIN_BUZZER, HIGH);
    buzzerAktif = true;
    waktuBuzzerMulai = sekarang;
    Serial.println(F("PERINGATAN: torsi melewati 0,50 N.m!"));
  } else if (alarmAktif && torsiNm < AMBANG_LEPAS_NM) {
    alarmAktif = false;
    digitalWrite(PIN_LED_ALARM, LOW);
    digitalWrite(PIN_BUZZER, LOW);
    buzzerAktif = false;
  }
}

// ---------------------------------------------------------------------------
//  bacaTombolTare — tombol ditekan (LOW) -> nolkan pembacaan (tare).
//  Debounce: abaikan perubahan yang terlalu cepat setelah perubahan terakhir.
// ---------------------------------------------------------------------------
void bacaTombolTare(unsigned long sekarang) {
  bool tombolSekarang = digitalRead(PIN_TOMBOL_TARE);

  if (tombolSekarang != tombolTareTerakhir) {
    if (sekarang - waktuTombolTerakhir >= DEBOUNCE_TOMBOL_MS) {
      waktuTombolTerakhir = sekarang;
      if (tombolSekarang == LOW) {
        tare();
      }
    }
    tombolTareTerakhir = tombolSekarang;
  }
}

// ---------------------------------------------------------------------------
//  tare — nolkan pembacaan, reset torsi tertinggi, beri konfirmasi.
// ---------------------------------------------------------------------------
void tare() {
  timbangan.tare(JUMLAH_SAMPEL_BACA);
  torsiTertinggiNm = 0.0;
  lcd.setCursor(0, 1);
  lcd.print(F("Nol (tare)      "));
  Serial.println(F("Tare: pembacaan torsi dinolkan."));
}

// ---------------------------------------------------------------------------
//  kirimStatusSerial — kirim torsi + nilai mentah ke Serial Monitor.
//  Nilai mentah berguna saat kalibrasi (lihat FAKTOR_KALIBRASI di atas).
// ---------------------------------------------------------------------------
void kirimStatusSerial(float torsiNm) {
  Serial.print(F("Torsi: "));
  Serial.print(torsiNm, 3);
  Serial.print(F(" N.m  |  maks: "));
  Serial.print(torsiTertinggiNm, 3);
  Serial.print(F("  |  nilai mentah: "));
  Serial.println(timbangan.read_average(JUMLAH_SAMPEL_BACA));
}
