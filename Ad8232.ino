#include <Wire.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "MAX30105.h"          // library SparkFun (dipakai juga untuk MAX30102)
#include "spo2_algorithm.h"

// ==========================================
// KONFIGURASI OLED DISPLAY 0.96"
// ==========================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==========================================
// DEFINISI PIN
// ==========================================
const int LED_PIN    = 2;   // LED bawaan ESP32
const int LED_HIJAU  = 25;
const int LED_KUNING = 26;
const int LED_MERAH  = 27;

// AD8232 (ECG)
const int ECG_PIN      = 34;  // OUTPUT AD8232 (ADC1)
const int ECG_LO_PLUS  = 32;  // LO+
const int ECG_LO_MINUS = 33;  // LO-

// DHT11 (data di D4)
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// DS18B20 (data di D5, WAJIB ada resistor 4.7k antara DATA dan 3.3V)
#define ONE_WIRE_BUS 5
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);

DeviceAddress dsAddr;
bool dsAddrOK = false;
bool dsPending = false;
unsigned long dsReqTime = 0;
unsigned long lastDsRequest = 0;
int dsFailCount = 0;
const unsigned long dsInterval = 1000;   // jeda antar request
const unsigned long dsWait = 250;        // waktu tunggu konversi (resolusi 10 bit ~190 ms)

// ==========================================
// MAX30102
// ==========================================
MAX30105 particleSensor;
bool maxOK = false;

const int BUF_LEN = 100;               // 100 sampel @25 sps = 4 detik
uint32_t irBuf[BUF_LEN], redBuf[BUF_LEN];   // buffer melingkar
uint32_t irLin[BUF_LEN], redLin[BUF_LEN];   // buffer lurus untuk algoritma
int bufHead = 0, bufCount = 0, newSamples = 0;

int32_t spo2Val, hrVal;
int8_t  spo2Valid, hrValid;
int hrOut = 0, spo2Out = 0;
bool fingerOn = false;

// ==========================================
// VARIABEL DATA SUHU
// ==========================================
float humid = NAN, tempDHT = NAN, tempDS18B20 = NAN;
bool dhtError = true, dsError = true;
String statusTeks = "ERROR";

// Timer
unsigned long lastTempRead = 0;
const long tempInterval = 2000;
unsigned long lastEcgSample = 0;
unsigned long lastRender = 0;
unsigned long lastBlink = 0;
bool ledState = false;

// ==========================================
// VARIABEL ECG AD8232
// ==========================================
const int ECG_BUF = 128;
int ecgBuf[ECG_BUF];
int ecgIdx = 0;
int ecgDecim = 0;
float ecgFilt = 2048, ecgBase = 2048, ecgMax = 0;
bool ecgLeadOff = true;
bool ecgAbove = false;
unsigned long lastBeatEcg = 0;
unsigned long ecgIntervals[4] = {0, 0, 0, 0};
int ecgIntIdx = 0;
int ecgBPM = 0;

// ==========================================
// PROSES DS18B20 (non-blocking, tahan error)
// ==========================================
void prosesDS18B20() {
  unsigned long now = millis();

  // Cari alamat sensor kalau belum ketemu
  if (!dsAddrOK && !dsPending && (now - lastDsRequest >= dsInterval)) {
    lastDsRequest = now;
    ds18b20.begin();
    if (ds18b20.getAddress(dsAddr, 0)) {
      dsAddrOK = true;
      ds18b20.setResolution(dsAddr, 10);
      ds18b20.setWaitForConversion(false);
    } else {
      dsFailCount++;
      if (dsFailCount >= 3) dsError = true;
    }
    return;
  }
  if (!dsAddrOK) return;

  // Kirim perintah konversi
  if (!dsPending && (now - lastDsRequest >= dsInterval)) {
    ds18b20.requestTemperatures();
    dsPending = true;
    dsReqTime = now;
    lastDsRequest = now;
    return;
  }

  // Baca hasil setelah konversi selesai
  if (dsPending && (now - dsReqTime >= dsWait)) {
    dsPending = false;
    float t = ds18b20.getTempC(dsAddr);

    if (t == DEVICE_DISCONNECTED_C || t <= -100.0 || t == 85.0) {
      dsFailCount++;
      if (dsFailCount >= 3) {
        dsError = true;
        dsAddrOK = false;   // paksa cari ulang sensor
      }
    } else {
      tempDS18B20 = t;
      dsError = false;
      dsFailCount = 0;
    }
  }
}

// ==========================================
// PROSES MAX30102 (non-blocking)
// ==========================================
void prosesMAX() {
  if (!maxOK) return;

  particleSensor.check();   // ambil data baru dari FIFO sensor

  while (particleSensor.available()) {
    uint32_t red = particleSensor.getFIFORed();
    uint32_t ir  = particleSensor.getFIFOIR();
    particleSensor.nextSample();

    fingerOn = (ir > 50000);   // jari menempel?
    if (!fingerOn) {
      bufCount = 0; newSamples = 0;
      hrOut = 0; spo2Out = 0;
      continue;
    }

    irBuf[bufHead]  = ir;
    redBuf[bufHead] = red;
    bufHead = (bufHead + 1) % BUF_LEN;
    if (bufCount < BUF_LEN) bufCount++;
    newSamples++;

    // Hitung ulang tiap 25 sampel baru (~1 detik)
    if (bufCount == BUF_LEN && newSamples >= 25) {
      newSamples = 0;
      for (int i = 0; i < BUF_LEN; i++) {
        int idx = (bufHead + i) % BUF_LEN;   // urut dari yang tertua
        irLin[i]  = irBuf[idx];
        redLin[i] = redBuf[idx];
      }
      maxim_heart_rate_and_oxygen_saturation(irLin, BUF_LEN, redLin,
                                             &spo2Val, &spo2Valid,
                                             &hrVal, &hrValid);

      if (hrValid && hrVal > 40 && hrVal < 200)
        hrOut = (hrOut > 0) ? (hrOut * 2 + hrVal) / 3 : hrVal;   // haluskan
      if (spo2Valid && spo2Val >= 80 && spo2Val <= 100)
        spo2Out = spo2Val;
    }
  }
}

// ==========================================
// PROSES ECG AD8232 (sampling ~200 Hz)
// ==========================================
void prosesECG() {
  unsigned long now = millis();
  if (now - lastEcgSample < 5) return;
  lastEcgSample = now;

  ecgLeadOff = digitalRead(ECG_LO_PLUS) || digitalRead(ECG_LO_MINUS);

  if (ecgLeadOff) {
    ecgBPM = 0;
    ecgMax = 0;
    ecgAbove = false;
    return;
  }

  int raw = analogRead(ECG_PIN);
  ecgFilt = ecgFilt * 0.7f + raw * 0.3f;          // low-pass sederhana
  ecgBase = ecgBase * 0.99f + ecgFilt * 0.01f;    // hilangkan baseline
  float sig = ecgFilt - ecgBase;

  // Simpan untuk grafik (decimasi 3x -> ~67 Hz)
  if (++ecgDecim >= 3) {
    ecgDecim = 0;
    ecgBuf[ecgIdx] = (int)sig;
    ecgIdx = (ecgIdx + 1) % ECG_BUF;
  }

  // Deteksi puncak R dengan threshold adaptif
  ecgMax = max(sig, ecgMax * 0.998f);
  float thr = ecgMax * 0.6f;

  if (ecgMax > 80 && sig > thr && !ecgAbove && (now - lastBeatEcg) > 300) {
    ecgAbove = true;
    unsigned long interval = now - lastBeatEcg;
    lastBeatEcg = now;
    if (interval < 1500) {
      ecgIntervals[ecgIntIdx] = interval;
      ecgIntIdx = (ecgIntIdx + 1) % 4;
      unsigned long sum = 0; int n = 0;
      for (int i = 0; i < 4; i++) if (ecgIntervals[i] > 0) { sum += ecgIntervals[i]; n++; }
      if (n > 0) ecgBPM = 60000UL / (sum / n);
    }
  }
  if (sig < thr) ecgAbove = false;

  if (now - lastBeatEcg > 2500) {
    ecgBPM = 0;
    for (int i = 0; i < 4; i++) ecgIntervals[i] = 0;
  }
}

// ==========================================
// STATUS, LED & SERIAL (tiap 2 detik)
// ==========================================
void bacaSuhu() {
  humid   = dht.readHumidity();
  tempDHT = dht.readTemperature();
  dhtError = isnan(humid) || isnan(tempDHT);

  statusTeks = "ERROR";
  digitalWrite(LED_HIJAU, LOW);
  digitalWrite(LED_KUNING, LOW);
  digitalWrite(LED_MERAH, LOW);

  if (!dsError) {
    if (tempDS18B20 < 30.0) {
      statusTeks = "AMAN";
      digitalWrite(LED_HIJAU, HIGH);
    } else if (tempDS18B20 <= 35.0) {
      statusTeks = "SIAGA";
      digitalWrite(LED_KUNING, HIGH);
    } else {
      statusTeks = "AWAS!";
      digitalWrite(LED_MERAH, HIGH);
    }
  }

  // --- Serial Monitor ---
  Serial.println("=== Farra, Tumenggung, & Afif ===");
  Serial.print("Status   -> "); Serial.println(statusTeks);

  if (dhtError) Serial.println("DHT11    -> Gagal baca (Cek kabel pin D4)");
  else { Serial.print("DHT11    -> Suhu: "); Serial.print(tempDHT); Serial.print("C | Hum: "); Serial.print(humid); Serial.println("%"); }

  if (dsError) Serial.println("DS18B20  -> Gagal baca (Cek resistor 4.7k & kabel D5!)");
  else { Serial.print("DS18B20  -> Suhu: "); Serial.print(tempDS18B20); Serial.println("C"); }

  if (!maxOK) Serial.println("MAX30102 -> Tidak terdeteksi (Cek SDA/SCL)");
  else if (!fingerOn) Serial.println("MAX30102 -> Jari belum menempel");
  else {
    Serial.print("MAX30102 -> HR: "); Serial.print(hrOut);
    Serial.print(" bpm | SpO2: "); Serial.print(spo2Out); Serial.println("%");
  }

  if (ecgLeadOff) Serial.println("AD8232   -> Lead OFF (elektroda belum menempel)");
  else { Serial.print("AD8232   -> ECG BPM: "); Serial.println(ecgBPM); }
  Serial.println("=================================\n");
}

// ==========================================
// HALAMAN 1: SUHU & KELEMBAPAN
// ==========================================
void halamanSuhu() {
  display.setCursor(0, 0);
  display.println("Tim:");
  display.println("Farra,Tumenggung,Afif");
  display.drawLine(0, 18, 128, 18, SSD1306_WHITE);

  display.setCursor(0, 22);
  display.print("Air  : ");
  if (dsError) display.println("Error");
  else { display.print(tempDS18B20, 1); display.println(" C"); }

  display.setCursor(0, 32);
  display.print("Suhu : ");
  if (dhtError) display.println("Error");
  else { display.print(tempDHT, 1); display.println(" C"); }

  display.setCursor(0, 42);
  display.print("Humid: ");
  if (dhtError) display.println("Error");
  else { display.print(humid, 1); display.println(" %"); }

  display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
  display.setCursor(0, 55);
  display.print("STATUS: ");
  display.print(statusTeks);
}

// ==========================================
// HALAMAN 2: HR, SpO2 (MAX30102) + ECG (AD8232)
// ==========================================
void halamanVital() {
  // Baris 1: MAX30102
  display.setCursor(0, 0);
  if (!maxOK) {
    display.print("MAX30102 tdk ada");
  } else if (!fingerOn) {
    display.print("Taruh jari...");
  } else {
    display.print("HR:");
    if (hrOut > 0) display.print(hrOut); else display.print("--");
    display.print(" SpO2:");
    if (spo2Out > 0) { display.print(spo2Out); display.print("%"); } else display.print("--");
  }

  // Ikon detak, berkedip mengikuti HR
  if (fingerOn && hrOut > 0) {
    unsigned long periode = 60000UL / hrOut;
    if ((millis() % periode) < 120) display.fillCircle(122, 4, 3, SSD1306_WHITE);
    else display.drawCircle(122, 4, 3, SSD1306_WHITE);
  } else {
    display.drawCircle(122, 4, 3, SSD1306_WHITE);
  }

  // Baris 2: AD8232
  display.setCursor(0, 9);
  display.print("ECG:");
  if (ecgLeadOff) display.print("Lead OFF");
  else { if (ecgBPM > 0) display.print(ecgBPM); else display.print("--"); display.print(" bpm"); }

  display.drawLine(0, 18, 128, 18, SSD1306_WHITE);

  // Grafik ECG (y: 20..63)
  if (ecgLeadOff) {
    display.setCursor(10, 38);
    display.print("Pasang elektroda");
    return;
  }

  int mn = 32767, mx = -32768;
  for (int i = 0; i < ECG_BUF; i++) {
    if (ecgBuf[i] < mn) mn = ecgBuf[i];
    if (ecgBuf[i] > mx) mx = ecgBuf[i];
  }
  if (mx - mn < 100) { int mid = (mx + mn) / 2; mn = mid - 50; mx = mid + 50; }

  int prevY = -1;
  for (int x = 0; x < ECG_BUF; x++) {
    int v = ecgBuf[(ecgIdx + x) % ECG_BUF];
    int y = constrain(map(v, mn, mx, 62, 21), 20, 63);
    if (x > 0) display.drawLine(x - 1, prevY, x, y, SSD1306_WHITE);
    prevY = y;
  }
}

// ==========================================
// RENDER OLED (ganti halaman tiap 4 detik)
// ==========================================
void renderOLED() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  int halaman = (millis() / 4000) % 2;
  if (halaman == 0) halamanSuhu();
  else halamanVital();

  display.display();
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  pinMode(LED_PIN, OUTPUT);
  pinMode(LED_HIJAU, OUTPUT);
  pinMode(LED_KUNING, OUTPUT);
  pinMode(LED_MERAH, OUTPUT);
  digitalWrite(LED_HIJAU, LOW);
  digitalWrite(LED_KUNING, LOW);
  digitalWrite(LED_MERAH, LOW);

  pinMode(ECG_LO_PLUS, INPUT);
  pinMode(ECG_LO_MINUS, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(ECG_PIN, ADC_11db);
  for (int i = 0; i < ECG_BUF; i++) ecgBuf[i] = 0;

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("OLED SSD1306 gagal diinisialisasi!"));
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 10);
  display.println(F("Sistem Monitoring"));
  display.setCursor(0, 25);
  display.println(F("Loading data..."));
  display.display();

  dht.begin();

  // DS18B20: cek sensor saat start
  ds18b20.begin();
  Serial.print("DS18B20 terdeteksi: ");
  Serial.println(ds18b20.getDeviceCount());
  if (ds18b20.getAddress(dsAddr, 0)) {
    dsAddrOK = true;
    ds18b20.setResolution(dsAddr, 10);
    ds18b20.setWaitForConversion(false);
  } else {
    Serial.println(F("DS18B20 TIDAK ditemukan! Cek resistor 4.7k, kabel D5, VCC & GND."));
  }

  // MAX30102
  if (particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    maxOK = true;
    byte ledBrightness = 60;   // 0-255
    byte sampleAverage = 4;
    byte ledMode = 2;          // Red + IR
    int  sampleRate = 100;     // 100 / 4 = 25 sampel per detik
    int  pulseWidth = 411;
    int  adcRange = 4096;
    particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  } else {
    Serial.println(F("MAX30102 gagal diinisialisasi! Cek SDA/SCL & power."));
  }

  Wire.setClock(400000);
  Serial.println("\nSistem Monitoring Dimulai...");
}

// ==========================================
// LOOP
// ==========================================
void loop() {
  unsigned long now = millis();

  prosesMAX();       // sering dipanggil agar FIFO tidak overflow
  prosesECG();       // ~200 Hz
  prosesDS18B20();   // non-blocking

  if (now - lastTempRead >= tempInterval) {
    lastTempRead = now;
    bacaSuhu();
  }

  if (now - lastRender >= 100) {   // ~10 fps
    lastRender = now;
    renderOLED();
  }

  if (now - lastBlink >= 500) {    // LED bawaan berkedip
    lastBlink = now;
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
  }
}