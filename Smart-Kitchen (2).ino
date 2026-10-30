#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ================= WIFI & SUPABASE =================
const char* WIFI_SSID     = "";   // Masukkan SSID WiFi Anda
const char* WIFI_PASSWORD = "";  // Masukkan Password WiFi Anda

// Konfigurasi Supabase REST API
const char* SUPABASE_URL  = ""; // Masukkkan URL Supabase
const char* SUPABASE_KEY  = ""; // Masukkan API KEY
const char* DEVICE_ID     = ""; //Device ID

// ================= TELEGRAM (OPSIONAL) =================
#define BOT_TOKEN "" //URL Bot
#define CHAT_ID   "" //Chat ID

const unsigned long INTERVAL_NOTIF_GAS  = 60000;    // 1 menit
const unsigned long INTERVAL_LAPORAN    = 1800000;  // 30 menit
const unsigned long INTERVAL_CEK_BOT    = 2000;

// ================= PIN NODEMCU =================
#define DHT_PIN     D5
#define DHT_TYPE    DHT22
#define MQ2_DO_PIN  D7
#define MQ2_AO_PIN  A0
#define BUZZER_PIN  D6      // D6 aman untuk ESP8266
#define LCD_SDA     D2
#define LCD_SCL     D1

DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C* lcd = nullptr;

WiFiClientSecure clientTelegram;
UniversalTelegramBot bot(BOT_TOKEN, clientTelegram);

// ================= AMBANG (PERSIS CODINGAN AWAL ANDA) =================
const float SUHU_ABS_ON    = 40.0;
const float SUHU_ABS_OFF   = 38.0;
const float SUHU_NAIK_ON   = 3.0;
const float SUHU_NAIK_OFF  = 1.5;

const int ASAP_ABS_ON_ASLI = 550;   // Nilai referensi asli
const int ASAP_DELTA_ON    = 30;
const int ASAP_DELTA_OFF   = 15;
const int ASAP_LONJAKAN    = 25;

const bool PAKAI_DO        = true;  // Menggunakan pin DO sensor MQ-2
const int  MQ2_AKTIF       = LOW;   // DO MQ-02 aktif LOW saat gas terdeteksi
const int  ASAP_ABS_ON     = 9999;  // 9999 persis koding awal Anda

const int  BUZZER_FREKUENSI = 2500;
const unsigned long BUZZER_INTERVAL = 500;

// ================= INTERVAL =================
const unsigned long MQ2_PEMANASAN_MS = 10000; // 10 detik pemanasan awal
const unsigned long INTERVAL_MQ     = 50;
const unsigned long INTERVAL_DHT    = 2500;
const unsigned long INTERVAL_LCD    = 500;
const unsigned long INTERVAL_SERIAL = 1000;
const unsigned long INTERVAL_KIRIM  = 10000;  // Kirim data berkala tiap 10 detik
const unsigned long INTERVAL_WIFI   = 15000;
const unsigned long ALARM_TAHAN_MS  = 2000;   // Tahan alarm 2 detik setelah gas hilang

const int KONFIRMASI_ON  = 2;  // 2x deteksi cepat (100 ms) langsung picu bahaya
const int KONFIRMASI_OFF = 10; // 10x aman (500 ms) langsung kembali normal

// ================= VARIABEL SISTEM =================
unsigned long tMQ = 0, tDHT = 0, tLCD = 0, tSerial = 0;
unsigned long tKirim = 0, tWiFi = 0, tGas = 0, tStart = 0, tBuzzer = 0;

float suhu = NAN, kelembapan = NAN;
float suhuBase = NAN;
bool dataDHTValid = false;
int gagalDHT = 0;

int statusDO = HIGH;
int asapRaw = 0;
float asapAvg = 0, baseline = 0;

int riwayat[10];
int idxRiwayat = 0;
bool riwayatPenuh = false;

int hitungGas = 0, hitungAman = 0;

bool mq2Siap = false;
bool gasTerdeteksi = false;
bool suhuNaik = false;
bool alarmAktif = false;

bool polaAktif = false;
bool polaOn = false;

const char* sebab = "-";

// Status sebelumnya untuk trigger kirim instan ke web
bool lastGasState = false;
bool lastAlarmState = false;

// Variabel Telegram
unsigned long lastGasNotif = 0;
unsigned long lastLaporan = 0;
unsigned long botLastScan = 0;
bool lastGasStatus = false;

// ================= FUNGSI BANTU =================
byte cariAlamatLCD() {
  byte kandidat[] = {0x27, 0x3F};
  for (byte i = 0; i < 2; i++) {
    Wire.beginTransmission(kandidat[i]);
    if (Wire.endTransmission() == 0) return kandidat[i];
  }
  return 0x27;
}

int bacaAO() {
  long total = 0;
  for (int i = 0; i < 4; i++) {
    total += analogRead(MQ2_AO_PIN);
    delayMicroseconds(100);
  }
  return total / 4;
}

bool bacaDHT() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (!isnan(h) && !isnan(t) && t > -40 && t < 80 && h >= 0 && h <= 100) {
    suhu = t;
    kelembapan = h;
    return true;
  }
  return false;
}

void cekWiFi(unsigned long now) {
  if (WiFi.status() == WL_CONNECTED) return;
  if (now - tWiFi >= INTERVAL_WIFI) {
    tWiFi = now;
    Serial.println("[WiFi] Reconnecting...");
    WiFi.reconnect();
  }
}

// ================= BUZZER =================
void kendaliBuzzer(unsigned long now) {
  if (alarmAktif) {
    if (!polaAktif) {
      polaAktif = true;
      polaOn = true;
      tBuzzer = now;
      tone(BUZZER_PIN, BUZZER_FREKUENSI);
    } else if (now - tBuzzer >= BUZZER_INTERVAL) {
      tBuzzer = now;
      polaOn = !polaOn;
      if (polaOn) tone(BUZZER_PIN, BUZZER_FREKUENSI);
      else        noTone(BUZZER_PIN);
    }
  } else if (polaAktif) {
    polaAktif = false;
    polaOn = false;
    noTone(BUZZER_PIN);
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// ================= LCD =================
void tampilkanLCD(unsigned long now) {
  if (!lcd) return;

  lcd->setCursor(0, 0);
  if (dataDHTValid) {
    char buf[8];
    dtostrf(suhu, 4, 1, buf);
    lcd->print("T:");
    lcd->print(buf);
    lcd->print((char)223);
    lcd->print("C H:");
    lcd->print((int)kelembapan);
    lcd->print("%   ");
  } else {
    lcd->print("DHT22 ERROR     ");
  }

  lcd->setCursor(0, 1);
  if (alarmAktif) {
    lcd->print("ALARM: BAHAYA!  ");
  } else if (gasTerdeteksi) {
    lcd->print("WASPADA: GAS    ");
  } else if (suhuNaik) {
    lcd->print("WASPADA: SUHU   ");
  } else if (!mq2Siap) {
    int sisa = (MQ2_PEMANASAN_MS - (now - tStart)) / 1000;
    if (sisa < 0) sisa = 0;
    lcd->print("MQ2 panas ");
    lcd->print(sisa);
    lcd->print("s    ");
  } else {
    lcd->print("STATUS: AMAN    ");
  }
}

// ================= KIRIM KE SUPABASE =================
void kirimDataServer() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(1024, 512);
  client.setTimeout(5000);

  HTTPClient http;
  http.setTimeout(5000);

  if (!http.begin(client, SUPABASE_URL)) return;

  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Prefer", "return=minimal");

  String payload = "{";
  payload += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
  payload += "\"temperature\":" + (dataDHTValid ? String(suhu, 2) : "null") + ",";
  payload += "\"humidity\":" + (dataDHTValid ? String(kelembapan, 2) : "null") + ",";
  payload += "\"gas_adc\":" + String((int)asapAvg) + ",";
  payload += "\"gas_detected\":" + String(gasTerdeteksi ? "true" : "false") + ",";
  payload += "\"suhu_naik\":" + String(suhuNaik ? "true" : "false") + ",";
  payload += "\"alarm_active\":" + String(alarmAktif ? "true" : "false");
  payload += "}";

  Serial.println("[Supabase] Mengirim data: " + payload);
  int httpCode = http.POST(payload);

  if (httpCode > 0) {
    Serial.print("[Supabase] HTTP Response code: ");
    Serial.println(httpCode);
    if (httpCode == 201 || httpCode == 200 || httpCode == 204) {
      Serial.println("[Supabase] BERHASIL! Data masuk ke database & web.");
    }
  } else {
    Serial.print("[Supabase] Gagal kirim: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}

// ================= TELEGRAM =================
String teksSuhu() {
  return dataDHTValid ? String(suhu, 1) : String("ERROR");
}

String teksHum() {
  return dataDHTValid ? String(kelembapan, 1) : String("ERROR");
}

void kirimTelegram(const String& chatId, const String& pesan) {
  if (strlen(BOT_TOKEN) == 0 || strlen(CHAT_ID) == 0) return;
  if (WiFi.status() != WL_CONNECTED) return;
  bool ok = bot.sendMessage(chatId, pesan, "");
  Serial.println(ok ? "Telegram terkirim." : "Telegram GAGAL kirim.");
}

void cekTelegram(unsigned long now) {
  if (strlen(BOT_TOKEN) == 0 || strlen(CHAT_ID) == 0) return;
  if (WiFi.status() != WL_CONNECTED) return;

  if (gasTerdeteksi && (!lastGasStatus || now - lastGasNotif >= INTERVAL_NOTIF_GAS)) {
    lastGasNotif = now;
    lastGasStatus = true;
    String pesan = "🚨 PERINGATAN DARURAT: KEBOCORAN GAS!\nKebocoran gas terdeteksi!\n\n";
    pesan += "📊 Nilai Sensor (ADC): " + String((int)asapAvg) + "\n";
    pesan += "🌡️ Suhu: " + teksSuhu() + " °C\n";
    pesan += "⚠️ Segera periksa tabung gas!";
    kirimTelegram(CHAT_ID, pesan);
  }

  if (!gasTerdeteksi && lastGasStatus) {
    lastGasStatus = false;
    String pesan = "✅ [GAS CLEAR] KONDISI KEMBALI AMAN\n📍 Lokasi: Dapur Utama\n";
    pesan += "📉 Level Gas: NORMAL (" + String((int)asapAvg) + ")";
    kirimTelegram(CHAT_ID, pesan);
  }

  if (now - lastLaporan >= INTERVAL_LAPORAN) {
    lastLaporan = now;
    String status = alarmAktif ? "BAHAYA 🚨" : "NORMAL / AMAN ✅";
    String pesan = "📊 LAPORAN MONITORING SUHU REAL-TIME\n\n";
    pesan += "• Suhu: " + teksSuhu() + " °C 🌡️\n";
    pesan += "• Kelembapan: " + teksHum() + " % 💧\n";
    pesan += "• Status: " + status;
    kirimTelegram(CHAT_ID, pesan);
  }

  if (now - botLastScan >= INTERVAL_CEK_BOT) {
    botLastScan = now;
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages) {
      for (int i = 0; i < numNewMessages; i++) {
        if (bot.messages[i].text == "/suhu") {
          String balasan = "📊 CEK REALTIME\n";
          balasan += "Suhu: " + teksSuhu() + " C\n";
          balasan += "Kelembapan: " + teksHum() + " %\n";
          balasan += "Gas: " + String(gasTerdeteksi ? "TERDETEKSI" : "Aman") + "\n";
          balasan += "Alarm: " + String(alarmAktif ? "BUNYI" : "OFF");
          kirimTelegram(bot.messages[i].chat_id, balasan);
        }
      }
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== MONITORING DAPUR MBG ===");

  pinMode(MQ2_DO_PIN, INPUT); // Persis kodingan awal Anda
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Wire.begin(LCD_SDA, LCD_SCL);
  lcd = new LiquidCrystal_I2C(cariAlamatLCD(), 16, 2);
  lcd->init();
  lcd->backlight();
  lcd->clear();
  lcd->setCursor(0, 0);
  lcd->print("Monitoring MBG");
  lcd->setCursor(0, 1);
  lcd->print("Memulai...");

  dht.begin();

  tone(BUZZER_PIN, BUZZER_FREKUENSI);
  delay(200);
  noTone(BUZZER_PIN);
  digitalWrite(BUZZER_PIN, LOW);

  asapAvg = bacaAO();
  baseline = asapAvg;
  for (int i = 0; i < 10; i++) riwayat[i] = (int)asapAvg;

  Serial.print("Menghubungkan ke WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  clientTelegram.setInsecure();
  clientTelegram.setBufferSizes(1024, 512);

  delay(1500);

  unsigned long now = millis();
  tStart = tDHT = tMQ = tLCD = tSerial = tKirim = tWiFi = now;
  lastLaporan = now;
  botLastScan = now;

  lcd->clear();
  Serial.println("Sistem siap. MQ-02 inisialisasi...");
}

// ================= LOOP =================
void loop() {
  unsigned long now = millis();

  // ---------- 1. MQ-02 (LOGIKA AWAL ANDA YANG SUDAH TERUJI) ----------
  if (now - tMQ >= INTERVAL_MQ) {
    tMQ = now;

    statusDO = digitalRead(MQ2_DO_PIN);
    asapRaw = bacaAO();
    asapAvg = asapAvg * 0.3 + asapRaw * 0.7;

    int lama = riwayat[idxRiwayat];
    riwayat[idxRiwayat] = (int)asapAvg;
    idxRiwayat = (idxRiwayat + 1) % 10;
    if (idxRiwayat == 0) riwayatPenuh = true;

    if (!mq2Siap) {
      baseline = baseline * 0.9 + asapAvg * 0.1;
      if (now - tStart >= MQ2_PEMANASAN_MS) {
        mq2Siap = true;
        baseline = asapAvg;
        Serial.println("MQ-02 siap.");
      }
    } else {
      float delta = asapAvg - baseline;

      // Pemicu berbasis DO (Digital Pin D7) seperti coding awal Anda
      bool doAktif     = PAKAI_DO && (statusDO == MQ2_AKTIF);
      bool absTinggi   = (asapAvg >= ASAP_ABS_ON);
      bool deltaTinggi = (delta >= ASAP_DELTA_ON);
      bool lonjakan    = riwayatPenuh && ((asapAvg - lama) >= ASAP_LONJAKAN);

      bool pemicu = doAktif || absTinggi || deltaTinggi || lonjakan;
      bool aman   = !doAktif && (asapAvg < ASAP_ABS_ON) && (delta < ASAP_DELTA_OFF);

      if (doAktif)          sebab = "DO";
      else if (absTinggi)   sebab = "AO";
      else if (deltaTinggi) sebab = "DELTA";
      else if (lonjakan)    sebab = "LONJAKAN";

      if (!gasTerdeteksi) {
        if (pemicu) hitungGas++;
        else        hitungGas = 0;

        if (hitungGas >= KONFIRMASI_ON) {
          gasTerdeteksi = true;
          hitungAman = 0;
          tGas = now;
        }

        if (!pemicu) {
          float a = 0.002;
          baseline = baseline * (1.0 - a) + asapAvg * a;
        }
      } else {
        if (aman) hitungAman++;
        else      hitungAman = 0;

        // Otomatis kembali normal setelah gas hilang
        if (hitungAman >= KONFIRMASI_OFF && now - tGas >= ALARM_TAHAN_MS) {
          gasTerdeteksi = false;
          hitungGas = 0;
          hitungAman = 0;
          sebab = "-";
        }
      }
    }
  }

  // ---------- 2. DHT22 (LOGIKA AWAL ANDA) ----------
  if (now - tDHT >= INTERVAL_DHT) {
    tDHT = now;

    if (bacaDHT()) {
      dataDHTValid = true;
      gagalDHT = 0;

      if (isnan(suhuBase)) suhuBase = suhu;

      // ---------- 3. DETEKSI SUHU NAIK ----------
      float naik = suhu - suhuBase;

      if (!suhuNaik) {
        if (suhu >= SUHU_ABS_ON || naik >= SUHU_NAIK_ON) {
          suhuNaik = true;
        }
      } else {
        if (suhu < SUHU_ABS_OFF && naik < SUHU_NAIK_OFF) {
          suhuNaik = false;
        }
      }

      if (!suhuNaik) {
        suhuBase = suhuBase * 0.98 + suhu * 0.02;
      }
    } else {
      gagalDHT++;
      if (gagalDHT >= 3) {
        dataDHTValid = false;
        suhuNaik = false;
      }
    }
  }

  // ---------- 4. LOGIKA ALARM ----------
  alarmAktif = mq2Siap && (gasTerdeteksi || suhuNaik);
  kendaliBuzzer(now);

  // ---------- 5. LCD ----------
  if (now - tLCD >= INTERVAL_LCD) {
    tLCD = now;
    tampilkanLCD(now);
  }

  // ---------- 6. SERIAL MONITOR ----------
  if (now - tSerial >= INTERVAL_SERIAL) {
    tSerial = now;

    Serial.print("Suhu: ");
    if (dataDHTValid) { Serial.print(suhu, 1); Serial.print(" C"); }
    else Serial.print("ERROR");

    Serial.print(" | Base: ");
    Serial.print(suhuBase, 1);

    Serial.print(" | RH: ");
    if (dataDHTValid) { Serial.print(kelembapan, 0); Serial.print("%"); }
    else Serial.print("ERROR");

    Serial.print(" | AO: ");       Serial.print((int)asapAvg);
    Serial.print(" | BaseAO: ");   Serial.print((int)baseline);
    Serial.print(" | DO: ");       Serial.print(statusDO == HIGH ? "HIGH" : "LOW");
    Serial.print(" | Gas: ");      Serial.print(gasTerdeteksi ? "YA (BAHAYA)" : "TIDAK (AMAN)");
    Serial.print(" | SuhuNaik: "); Serial.print(suhuNaik ? "YA (BAHAYA)" : "TIDAK (NORMAL)");
    Serial.print(" | ALARM: ");    Serial.print(alarmAktif ? "BUNYI" : "OFF");
    if (gasTerdeteksi || suhuNaik) { Serial.print(" | Pemicu: "); Serial.print(sebab); }
    Serial.println();
  }

  // ---------- 7. WIFI & SUPABASE ----------
  cekWiFi(now);
  
  // Kirim instan begitu ada perubahan status (BAHAYA <-> AMAN) 
  // atau berkala tiap INTERVAL_KIRIM (10 detik)
  bool statusBerubah = (gasTerdeteksi != lastGasState) || (alarmAktif != lastAlarmState);
  if (statusBerubah || (now - tKirim >= INTERVAL_KIRIM)) {
    tKirim = now;
    lastGasState = gasTerdeteksi;
    lastAlarmState = alarmAktif;
    kirimDataServer();
  }

  // ---------- 8. TELEGRAM ----------
  cekTelegram(now);

  yield();
}