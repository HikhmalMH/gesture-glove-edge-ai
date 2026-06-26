#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Library TensorFlow Lite Micro
#include <TensorFlowLite_ESP32.h>
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

// Include file model AI
#include "model.h"

// Parameter StandardScaler hasil training (WAJIB, dihasilkan oleh sel 4 notebook)
// Berisi: NUM_FEATURES, scaler_mean[], scaler_std[]
#include "scaler_params.h"

// ================= PENGATURAN OLED & SENSOR =================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const int MPU_ADDR = 0x68;

// ================= PENGATURAN AI & BUFFER =================
const int WINDOW_SIZE = 150;
const int STEP_SIZE = 75;
float window[WINDOW_SIZE][6];
int sample_count = 0;

tflite::ErrorReporter* error_reporter = nullptr;
const tflite::Model* model_tflite = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;
TfLiteTensor* output = nullptr;

constexpr int kTensorArenaSize = 60 * 1024;
uint8_t tensor_arena[kTensorArenaSize];

// ====================================================================
// Urutan label WAJIB sama dengan label_mapping di notebook:
//   {idle:0, wave:1, rotate:2, push:3, lift:4}  -- BUKAN alfabetis.
// Index output model mengikuti urutan ini.
// ====================================================================
const char* LABELS[] = {"idle", "wave", "rotate", "push", "lift"};

void setup() {
  Serial.begin(115200);

  // Khusus ESP32-S3 native USB, tunggu serial monitor terbuka
  while (!Serial) { delay(10); }

  Serial.println("=== Memulai Inisialisasi Sistem Deployment ===");

  Wire.begin(8, 9);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED gagal diinisialisasi"));
    for(;;);
  }
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(10, 20);
  display.print("BOOTING AI");
  display.display();

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);

  static tflite::MicroErrorReporter micro_error_reporter;
  error_reporter = &micro_error_reporter;

  model_tflite = tflite::GetModel(g_model);
  if (model_tflite->version() != TFLITE_SCHEMA_VERSION) {
    Serial.println("Versi skema model tidak cocok!");
    return;
  }

  static tflite::AllOpsResolver resolver;
  static tflite::MicroInterpreter static_interpreter(
      model_tflite, resolver, tensor_arena, kTensorArenaSize, error_reporter);
  interpreter = &static_interpreter;

  interpreter->AllocateTensors();
  input = interpreter->input(0);
  output = interpreter->output(0);

  Serial.println("TFLite Micro berhasil dialokasikan. AI SIAP!");
  display.clearDisplay();
  display.setCursor(10, 20);
  display.print("AI READY!");
  display.display();
  delay(1000);
}

void loop() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 14, true);

  int16_t raw_ax = (Wire.read() << 8 | Wire.read());
  int16_t raw_ay = (Wire.read() << 8 | Wire.read());
  int16_t raw_az = (Wire.read() << 8 | Wire.read());
  int16_t raw_temp = (Wire.read() << 8 | Wire.read());
  int16_t raw_gx = (Wire.read() << 8 | Wire.read());
  int16_t raw_gy = (Wire.read() << 8 | Wire.read());
  int16_t raw_gz = (Wire.read() << 8 | Wire.read());

  // Pembacaan sensor LANGSUNG tanpa filter (konsisten dengan data training yang mentah)
  // Skala: accel /16384 (±2g), gyro /131 (±250 dps)
  window[sample_count][0] = raw_ax / 16384.0;
  window[sample_count][1] = raw_ay / 16384.0;
  window[sample_count][2] = raw_az / 16384.0;
  window[sample_count][3] = raw_gx / 131.0;
  window[sample_count][4] = raw_gy / 131.0;
  window[sample_count][5] = raw_gz / 131.0;

  sample_count++;

  if (sample_count == WINDOW_SIZE) {
    int feature_idx = 0;

    for (int axis = 0; axis < 6; axis++) {
      float sum = 0;
      float sq_sum = 0;
      float min_val = window[0][axis];
      float max_val = window[0][axis];

      for (int i = 0; i < WINDOW_SIZE; i++) {
        float val = window[i][axis];
        sum += val;
        sq_sum += val * val;
        if (val < min_val) min_val = val;
        if (val > max_val) max_val = val;
      }

      float mean_val  = sum / WINDOW_SIZE;
      float rms_val   = sqrt(sq_sum / WINDOW_SIZE);
      float range_val = max_val - min_val;

      float var_sum = 0;
      for (int i = 0; i < WINDOW_SIZE; i++) {
        var_sum += pow(window[i][axis] - mean_val, 2);
      }
      float std_val = sqrt(var_sum / WINDOW_SIZE);

      // === Normalisasi StandardScaler: (x - mean) / std ===
      // WAJIB: MLP dilatih pada fitur yang sudah di-scale. Tanpa ini prediksi ngawur.
      // Urutan fitur per sumbu: mean, std, min, max, range, rms (cocok dgn notebook).
      input->data.f[feature_idx] = (mean_val  - scaler_mean[feature_idx]) / scaler_std[feature_idx]; feature_idx++;
      input->data.f[feature_idx] = (std_val   - scaler_mean[feature_idx]) / scaler_std[feature_idx]; feature_idx++;
      input->data.f[feature_idx] = (min_val   - scaler_mean[feature_idx]) / scaler_std[feature_idx]; feature_idx++;
      input->data.f[feature_idx] = (max_val   - scaler_mean[feature_idx]) / scaler_std[feature_idx]; feature_idx++;
      input->data.f[feature_idx] = (range_val - scaler_mean[feature_idx]) / scaler_std[feature_idx]; feature_idx++;
      input->data.f[feature_idx] = (rms_val   - scaler_mean[feature_idx]) / scaler_std[feature_idx]; feature_idx++;
    }

    // Eksekusi Model
    interpreter->Invoke();

    // Cari probabilitas tertinggi
    int max_idx = 0;
    float max_val = output->data.f[0];
    for (int i = 1; i < 5; i++) {
      if (output->data.f[i] > max_val) {
        max_val = output->data.f[i];
        max_idx = i;
      }
    }

    String tebakan = LABELS[max_idx];

    // ================== OUTPUT SERIAL MONITOR ==================
    Serial.println("\n--- [RAW PROBABILITAS AI] ---");
    for (int i = 0; i < 5; i++) {
      Serial.print("Index ["); Serial.print(i); Serial.print("] (");
      Serial.print(LABELS[i]); Serial.print("): ");
      Serial.println(output->data.f[i], 4);
    }
    Serial.print("KESIMPULAN AI MENEBAK: "); Serial.println(tebakan);
    Serial.println("--------------------------------");

    // Tampilkan ke OLED
    display.clearDisplay();
    display.setTextSize(3);
    display.setCursor(10, 20);
    display.print(tebakan);
    display.display();

    // Geser buffer (sliding window 50% overlap)
    for (int i = 0; i < (WINDOW_SIZE - STEP_SIZE); i++) {
      for (int axis = 0; axis < 6; axis++) {
        window[i][axis] = window[i + STEP_SIZE][axis];
      }
    }
    sample_count = WINDOW_SIZE - STEP_SIZE;
  }
  delay(10);
}