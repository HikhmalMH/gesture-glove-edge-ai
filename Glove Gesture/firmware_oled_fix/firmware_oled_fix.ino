#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define I2C_SDA      8
#define I2C_SCL      9
#define MPU_ADDR     0x68

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR    0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define SAMPLE_RATE_HZ   100
const unsigned long SAMPLE_PERIOD_US = 1000000UL / SAMPLE_RATE_HZ;

#define REG_PWR_MGMT_1   0x6B
#define REG_SMPLRT_DIV   0x19
#define REG_CONFIG       0x1A
#define REG_GYRO_CONFIG  0x1B
#define REG_ACCEL_CONFIG 0x1C
#define REG_ACCEL_XOUT_H 0x3B
#define REG_WHO_AM_I     0x75

const float ACCEL_SCALE = 16384.0;
const float GYRO_SCALE  = 131.0;

unsigned long lastSampleTime = 0;
String currentLabel = "idle";
bool   recording    = false;

// [OLED] lacak perubahan biar refresh hanya saat perlu
String  lastShownLabel = "";
bool    lastShownRec   = false;
unsigned long lastOledMs = 0;

// ================= KALMAN FILTER CLASS =================
class SimpleKalman {
  private:
    float err_measure;
    float err_estimate;
    float q;
    float current_estimate;
    float last_estimate;
    float kalman_gain;

  public:
    SimpleKalman(float mea_e, float est_e, float q) {
      err_measure = mea_e;
      err_estimate = est_e;
      this->q = q;
      last_estimate = 0.0;
    }

    float updateEstimate(float mea) {
      kalman_gain = err_estimate / (err_estimate + err_measure);
      current_estimate = last_estimate + kalman_gain * (mea - last_estimate);
      err_estimate =  (1.0 - kalman_gain) * err_estimate + fabs(last_estimate - current_estimate) * q;
      last_estimate = current_estimate;
      return current_estimate;
    }
};

// Inisialisasi 6 objek Kalman untuk masing-masing axis
// Parameter: (Measurement Error, Estimate Error, Process Noise)
// Lu bisa tuning parameter 'q' (0.01) kalau ngerasa kurang smooth atau terlalu delay
SimpleKalman kf_ax(2.0, 2.0, 0.01);
SimpleKalman kf_ay(2.0, 2.0, 0.01);
SimpleKalman kf_az(2.0, 2.0, 0.01);
SimpleKalman kf_gx(2.0, 2.0, 0.01);
SimpleKalman kf_gy(2.0, 2.0, 0.01);
SimpleKalman kf_gz(2.0, 2.0, 0.01);
// =======================================================

void writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

uint8_t readRegister(uint8_t reg) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.endTransmission(true); // [FIX] true: lepas bus penuh, hindari konflik OLED
  Wire.requestFrom(MPU_ADDR, (uint8_t)1);
  return Wire.read();
}

bool initMPU6050() {
  uint8_t whoami = readRegister(REG_WHO_AM_I);
  Serial.print("WHO_AM_I = 0x");
  Serial.println(whoami, HEX);
  if (whoami == 0xFF || whoami == 0x00) {
    Serial.println("ERROR: Sensor tidak merespons! Cek wiring.");
    return false;
  }

  Serial.print("Sensor terdeteksi (WHO_AM_I=0x");
  Serial.print(whoami, HEX);
  Serial.println("), lanjut inisialisasi...");

  writeRegister(REG_PWR_MGMT_1, 0x00);
  delay(100);
  writeRegister(REG_CONFIG, 0x03);
  writeRegister(REG_SMPLRT_DIV, 0x09);
  writeRegister(REG_GYRO_CONFIG, 0x00);
  writeRegister(REG_ACCEL_CONFIG, 0x00);
  delay(100);
  return true;
}

void readSensor(float &ax, float &ay, float &az,
                float &gx, float &gy, float &gz) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(REG_ACCEL_XOUT_H);
  Wire.endTransmission(true);   // [FIX] true: lepas bus penuh sebelum read
  Wire.requestFrom(MPU_ADDR, (uint8_t)14);

  int16_t rawAx = (Wire.read() << 8) | Wire.read();
  int16_t rawAy = (Wire.read() << 8) | Wire.read();
  int16_t rawAz = (Wire.read() << 8) | Wire.read();
  Wire.read(); Wire.read(); // Skip temperature
  int16_t rawGx = (Wire.read() << 8) | Wire.read();
  int16_t rawGy = (Wire.read() << 8) | Wire.read();
  int16_t rawGz = (Wire.read() << 8) | Wire.read();

  // Convert and Apply Kalman Filter
  ax = kf_ax.updateEstimate(rawAx / ACCEL_SCALE);
  ay = kf_ay.updateEstimate(rawAy / ACCEL_SCALE);
  az = kf_az.updateEstimate(rawAz / ACCEL_SCALE);
  gx = kf_gx.updateEstimate(rawGx / GYRO_SCALE);
  gy = kf_gy.updateEstimate(rawGy / GYRO_SCALE);
  gz = kf_gz.updateEstimate(rawGz / GYRO_SCALE);
}

// [OLED] tampilkan gestur + status
void showGestureOLED() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("GESTUR AKTIF:");

  // nama gestur (besar, di tengah)
  display.setTextSize(2);
  display.setCursor(0, 22);
  display.println(currentLabel);

  // status rekam (bukan nama gestur, biar gak rancu)
  display.setTextSize(1);
  display.setCursor(0, 54);
  display.println(recording ? "STATUS: REKAM" : "STATUS: STANDBY");
  display.display();

  // catat apa yang barusan ditampilkan
  lastShownLabel = currentLabel;
  lastShownRec   = recording;
}

void printHelp() {
  Serial.println("------------------------------------------------");
  Serial.println("PERINTAH (ketik di Serial Monitor, akhiri Enter):");
  Serial.println("  label <nama>  -> set gestur (mis: label wave)");
  Serial.println("  start         -> mulai merekam data");
  Serial.println("  stop          -> berhenti merekam");
  Serial.println("  help          -> tampilkan bantuan ini");
  Serial.println("------------------------------------------------");
  Serial.println("Gestur: idle, wave, rotate, push, lift");
  Serial.println();
}

void handleSerialCommand() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd.startsWith("label ")) {
    currentLabel = cmd.substring(6);
    currentLabel.trim();
    Serial.print("# Label diset ke: ");
    Serial.println(currentLabel);
  }
  else if (cmd == "start") {
    recording = true;
    lastSampleTime = micros();
    Serial.print("# MULAI merekam label: ");
    Serial.println(currentLabel);
    Serial.println("participant_id,session_id,repetition_id,label,timestamp_ms,ax,ay,az,gx,gy,gz");
  }
  else if (cmd == "stop") {
    recording = false;
    Serial.println("# BERHENTI merekam.");
  }
  else if (cmd == "help") {
    printHelp();
  }
  else {
    Serial.print("# Perintah tidak dikenal: ");
    Serial.println(cmd);
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.clearDisplay();
  display.display();

  Serial.println("=== Inisialisasi MPU6050 ===");
  if (!initMPU6050()) {
    Serial.println("Inisialisasi GAGAL. Periksa koneksi lalu reset.");
    while (1) { delay(1000); }
  }
  Serial.println("MPU6050 siap.");
  Serial.println();
  printHelp();

  showGestureOLED();
}

void loop() {
  handleSerialCommand();
  
  // [OLED] refresh otomatis HANYA kalau ada perubahan label/status.
  // Cek tiap 100ms supaya gak ganggu sampling & gak spam bus I2C.
  if (millis() - lastOledMs >= 100) {
    lastOledMs = millis();
    if (currentLabel != lastShownLabel || recording != lastShownRec) {
      showGestureOLED();
    }
  }

  unsigned long now = micros();
  if (recording && (now - lastSampleTime >= SAMPLE_PERIOD_US)) {
    lastSampleTime += SAMPLE_PERIOD_US;
    float ax, ay, az, gx, gy, gz;
    readSensor(ax, ay, az, gx, gy, gz);

    // Hardcode ID sementara buat struktur CSV. Nanti lu ubah pas collect data beneran.
    Serial.print("P1,S1,R1,"); 
    Serial.print(currentLabel);   Serial.print(',');
    Serial.print(millis());       Serial.print(',');
    Serial.print(ax, 4); Serial.print(',');
    Serial.print(ay, 4); Serial.print(',');
    Serial.print(az, 4); Serial.print(',');
    Serial.print(gx, 4); Serial.print(',');
    Serial.print(gy, 4); Serial.print(',');
    Serial.println(gz, 4);
  }
}