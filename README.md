# 🍳 Smart Kitchen - Sistem Monitoring Dapur MBG
### Satuan Pelayanan Pemenuhan Gizi Nasional

Sistem monitoring cerdas berbasis IoT untuk mendeteksi kebocoran gas dan kenaikan suhu di dapur MBG secara real-time dengan notifikasi Telegram dan Dashboard Cloud.

## ⚙️ Hardware
- ESP8266 NodeMCU
- MQ-2 Gas Sensor (A0, D7)
- DHT22 (D5)
- LCD I2C 16x2 (D1, D2)
- Buzzer (D6)

## 🧠 Algoritma Unggulan
1. **Smart Filtering:** Moving Average 4x + EMA 0.3/0.7
2. **Adaptive Baseline:** Auto-update baseline saat aman
3. **State Confirmation:** KONFIRMASI_ON=2, KONFIRMASI_OFF=10
4. **Instant Push:** Kirim ke Supabase langsung saat status BAHAYA

## 🔄 Alur Kerja
INISIALISASI -> PEMANASAN MQ2 10s -> LOOP (Baca+Filter+Delta+Konfirmasi) -> Buzzer/LCD -> Supabase & Telegram

## ☁️ Supabase
Tabel: sensor_readings
Kolom: device_id, temperature, humidity, gas_adc, gas_detected, suhu_naik, alarm_active

## 🚀 Cara Install
1. Install library: DHT, LiquidCrystal I2C
2. Edit SSID, PASS, SUPABASE_URL, BOT_TOKEN di .ino
3. Board: NodeMCU 1.0, Upload

## 📊 Payload JSON
{"device_id": "...", "temperature": 32.15, "gas_adc": 412, "alarm_active": true}
