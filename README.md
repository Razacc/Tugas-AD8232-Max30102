# Sistem Monitoring Suhu & Vital Sign Berbasis ESP32

Proyek Medis Instrumen oleh **Farra, Tumenggung, & Afif**.

Sistem ini membaca suhu air, suhu dan kelembapan udara, detak jantung, SpO₂, dan sinyal ECG. Hasilnya ditampilkan di OLED 0.96" dan Serial Monitor, dengan indikator status lewat LED.

> **Disclaimer:** Proyek ini hanya untuk keperluan edukasi dan **bukan alat medis**. Jangan dipakai untuk diagnosis.

## Fitur

- Pembacaan suhu air (DS18B20) dengan status **AMAN / SIAGA / AWAS** dan LED hijau / kuning / merah
- Pembacaan suhu dan kelembapan udara (DHT11)
- Pembacaan detak jantung dan SpO₂ (MAX30102)
- Pembacaan sinyal ECG, BPM, dan grafik gelombang (AD8232)
- Deteksi *lead off* untuk elektroda ECG
- Tampilan OLED 2 halaman yang berganti otomatis tiap 4 detik
- Pembacaan sensor non-blocking, jadi semua sensor bisa berjalan bersamaan

## Komponen

| Komponen | Fungsi |
|---|---|
| ESP32 | Mikrokontroler |
| DHT11 | Suhu dan kelembapan udara |
| DS18B20 | Suhu air |
| MAX30102 | Heart rate dan SpO₂ |
| AD8232 + elektroda | Sinyal ECG |
| OLED SSD1306 0.96" (I2C) | Layar tampilan |
| LED hijau, kuning, merah | Indikator status |
| Resistor 4.7 kΩ | Pull-up DS18B20 |

## Wiring

| Komponen | Pin modul | Pin ESP32 |
|---|---|---|
| OLED | SDA / SCL | GPIO 21 / 22 |
| MAX30102 | SDA / SCL | GPIO 21 / 22 (satu jalur dengan OLED) |
| DHT11 | DATA | GPIO 4 |
| DS18B20 | DATA | GPIO 5 (+ resistor 4.7 kΩ ke 3.3V) |
| AD8232 | OUTPUT | GPIO 34 |
| AD8232 | LO+ | GPIO 32 |
| AD8232 | LO- | GPIO 33 |
| LED hijau | Anoda | GPIO 25 |
| LED kuning | Anoda | GPIO 26 |
| LED merah | Anoda | GPIO 27 |

Semua sensor memakai daya **3.3V** dan GND yang sama. Alamat I2C: OLED `0x3C`, MAX30102 `0x57`.

## Library yang dibutuhkan

Install lewat Arduino IDE (Library Manager):

- DHT sensor library (Adafruit)
- OneWire (Paul Stoffregen)
- DallasTemperature
- Adafruit GFX Library
- Adafruit SSD1306
- SparkFun MAX3010x Pulse and Proximity Sensor Library

## Cara Pakai

1. Rangkai komponen sesuai tabel wiring.
2. Buka `Ad8232.ino` di Arduino IDE.
3. Pilih board **ESP32 Dev Module** dan port yang sesuai.
4. Upload kode, lalu buka Serial Monitor dengan baud rate **115200**.

### Tampilan OLED

| Halaman | Isi |
|---|---|
| 1 | Nama tim, suhu air, suhu udara, kelembapan, status |
| 2 | HR dan SpO₂ (MAX30102), BPM ECG, grafik gelombang ECG |

### Status suhu air

| Suhu | Status | LED |
|---|---|---|
| < 30 °C | AMAN | Hijau |
| 30 – 35 °C | SIAGA | Kuning |
| > 35 °C | AWAS! | Merah |

### Tips pengukuran

- **MAX30102:** tempelkan jari dengan tekanan ringan dan jangan bergerak. Angka muncul sekitar 5–10 detik setelah jari menempel.
- **AD8232:** pasang elektroda RA (kanan atas), LA (kiri atas), dan RL (perut kanan bawah). Selama elektroda belum menempel, layar menampilkan "Lead OFF".
- **DS18B20:** wajib memakai resistor pull-up 4.7 kΩ antara kabel DATA dan 3.3V. Tanpa itu sensor tidak terbaca.

## Struktur Folder

```
Ad8232/
├── Ad8232.ino
└── README.md
```

## Anggota Tim

- Farra
- Tumenggung
- Afif
