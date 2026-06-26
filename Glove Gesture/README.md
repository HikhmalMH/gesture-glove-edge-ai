# MPU6050 Hand Gesture Classification (Edge AI Pipeline)

Proyek ini adalah implementasi *Machine Learning end-to-end* untuk mengklasifikasikan 5 jenis gestur tangan secara *real-time* menggunakan sensor MPU6050 dan mikrokontroler ESP32-S3. Seluruh proses inferensi berjalan sepenuhnya secara lokal di perangkat (*on-device Edge AI*) tanpa membutuhkan koneksi internet, menjadikannya purwarupa (*prototype*) berskala laboratorium yang ringan dan efisien.

Sistem membaca *raw data* 6-sumbu (Accelerometer & Gyroscope), memprosesnya dengan teknik *Sliding Window* (150 sampel, overlap 50%), mengekstraksi 36 fitur statistik, melakukan normalisasi *StandardScaler*, lalu mengumpankannya ke model **Multilayer Perceptron (MLP)** berbasis TensorFlow Lite Micro.

> **Catatan desain:** Data latih direkam sebagai sinyal mentah (tanpa filter). Agar distribusi fitur saat inferensi konsisten dengan saat pelatihan, firmware *deployment* juga membaca sensor secara langsung **tanpa Kalman Filter**. Konsistensi train–inference ini lebih diutamakan daripada penghalusan sinyal.

---

## 📌 Informasi Kelompok

* **Mata Kuliah:** ABK4ABB3 – Pembelajaran Mesin dan Aplikasi
* **Kelompok:** _(isi nomor kelompok)_
* **Anggota:**
  1. _(Hikhmal Muhammad Haykhal, 1102223059)_
  2. _(Daffa Abdul Aziz, 1102223248)_
  3. _(Ramzy Fawwaz, 1102223113)_
  4. _(Irvan Maulana Juanda, 1102223174)_
  5. _(Dzaki Wahib Rizqulloh, 1102220048)_
  6. _(Rizqy Pratama Athaurrahman, 1102220274)_

## 🔗 Tautan Penting (Wajib Diisi)

* **Repositori GitHub:** _(tautan repo publik aktif)_
* **Video Demo (YouTube):** _(tautan video demo on-device, maks. 15 menit)_
* **Paper Ilmiah (IEEE):** _(tautan/lokasi berkas PDF di repo, mis. `/docs/paper_ieee.pdf`)_
* **Laporan Akhir Teknis:** _(tautan/lokasi berkas PDF di repo, mis. `/docs/laporan_teknis.pdf`)_
* **Dataset:** disertakan di dalam repo pada folder `./data/`

---

## 🎯 Kelas Gestur

Sistem dirancang untuk mengenali 5 gestur berikut. Urutan indeks ini **wajib konsisten** antara notebook pelatihan dan firmware:

| Indeks | Label | Deskripsi |
|:---:|:---|:---|
| 0 | `idle` | Diam / posisi netral |
| 1 | `wave` | Melambai kiri–kanan |
| 2 | `rotate` | Memutar pergelangan |
| 3 | `push` | Mendorong ke depan |
| 4 | `lift` | Mengangkat ke atas |

---

## 🛠️ Persiapan Hardware (Perangkat Keras)

* **Mikrokontroler:** ESP32-S3 (atau varian ESP32 lain)
* **Sensor:** MPU6050 (Akselerometer & Giroskop 6-DOF)
* **Display:** OLED SSD1306 128x64 (menampilkan hasil prediksi *real-time*)
* **Koneksi:** Kabel jumper & breadboard

### Wiring I2C

* `SDA` MPU6050 & OLED → Pin 8 (ESP32-S3)
* `SCL` MPU6050 & OLED → Pin 9 (ESP32-S3)

> *Sesuaikan definisi pin di file `.ino` jika menggunakan board ESP32 jenis lain.*

## 💻 Persiapan Software (Perangkat Lunak)

* Python 3.10 atau lebih baru.
* Arduino IDE dengan *library*: `Adafruit_GFX`, `Adafruit_SSD1306`, dan `TensorFlowLite_ESP32`.
* *Library* Python:
  ```bash
  pip install pyserial pandas numpy scikit-learn tensorflow matplotlib seaborn
  ```

---

## 📊 1. Pengambilan Data (Data Collection)

Tahap pertama adalah merekam *raw data* gerakan untuk melatih model. Data direkam sebagai sinyal mentah dari sensor.

1. *Upload* kode firmware perekam ke ESP32 melalui Arduino IDE.
2. Tutup Serial Monitor pada Arduino IDE agar *port* tidak bentrok.
3. Buka terminal PC pada direktori proyek.
4. Jalankan skrip perekam dengan menyertakan nama partisipan:
   ```bash
   python serialtocsv.py <nama_partisipan>
   ```
5. Ikuti instruksi di terminal. Ketik angka `1`–`5` untuk berganti target gestur (tersedia *auto-pause* saat transisi).
6. Tekan `Ctrl+C` untuk berhenti. Data mentah tersimpan otomatis sebagai `.csv` di folder `./data/`.

> Setiap partisipan menghasilkan satu berkas CSV berisi kolom `participant_id, session_id, repetition_id, label, timestamp_ms, ax, ay, az, gx, gy, gz`.

---

## 🧠 2. Pelatihan Model (Training Pipeline)

Setelah data terkumpul dari beberapa partisipan, fitur diekstraksi dan model dilatih. Pemisahan data dilakukan **per partisipan** (*Leave-One-Subject-Out*) untuk mencegah *data leakage* — model diuji pada orang yang tidak ikut dalam pelatihan.

1. Buka `training_pipeline.ipynb` di Jupyter Notebook atau VS Code.
2. Pilih *kernel* Python yang memuat TensorFlow dan Scikit-Learn.
3. Pastikan folder data berisi berkas CSV, lalu jalankan seluruh *cell* secara berurutan (*Run All*).
4. *Notebook* mengekstraksi **36 fitur statistik** (mean, std, min, max, range, RMS untuk tiap 6 sumbu) dari *window* 150 sampel.
5. Sistem melatih tiga model untuk perbandingan: **Dummy Classifier** (baseline), **Random Forest** (pembanding, dengan *GridSearchCV* + *GroupKFold*), dan **MLP** (model yang di-deploy). Evaluasi mencakup *Accuracy, Precision, Recall, F1-Score*, dan *Confusion Matrix*, ditambah evaluasi *cross-subject*.
6. Pada akhir proses dihasilkan **dua berkas** yang wajib disalin ke folder firmware:
   * `model.h` — model MLP dalam format C array (TFLite Micro).
   * `scaler_params.h` — parameter normalisasi *StandardScaler* (`scaler_mean`, `scaler_std`).

> **Penting:** `model.h` dan `scaler_params.h` **harus** berasal dari satu kali eksekusi notebook yang sama agar model dan parameter normalisasi tetap sinkron.

---

## 🚀 3. Edge Deployment (Inferensi On-Device)

Langkah terakhir adalah menanamkan model ke mikrokontroler agar sistem memprediksi gestur secara mandiri.

1. Siapkan folder sketch Arduino (mis. `deployment_inference/`) berisi:
   ```
   deployment_inference/
   ├── deployment_inference.ino
   ├── model.h
   └── scaler_params.h
   ```
   Salin `model.h` **dan** `scaler_params.h` hasil notebook ke folder ini. *(Gunakan skrip deployment, bukan firmware perekam dari Tahap 1.)*
2. Buka `deployment_inference.ino` di Arduino IDE. Skrip ini memuat logika *Sliding Window*, ekstraksi 36 fitur, normalisasi *StandardScaler*, dan inferensi MLP secara C++.
3. *Compile* dan *Upload* ke ESP32. Inferensi dijalankan menggunakan `AllOpsResolver` dari TensorFlow Lite Micro.
4. Hubungkan ESP32 ke sumber daya. Peragakan gestur — hasil tebakan tampil *real-time* di OLED dan dapat dipantau melalui Serial Monitor (115200 baud), lengkap dengan probabilitas tiap kelas.

> **Konsistensi yang wajib dijaga di firmware:**
> - `#include "scaler_params.h"` dan normalisasi `(fitur − mean) / std` diterapkan sebelum inferensi.
> - Urutan `LABELS[]` = `{"idle", "wave", "rotate", "push", "lift"}` (sesuai indeks pelatihan, **bukan** urutan alfabetis).

---

## 📁 Struktur Repositori

```
.
├── data/                       # dataset CSV per partisipan
├── training_pipeline.ipynb     # notebook ekstraksi fitur + pelatihan
├── serialtocsv.py              # skrip perekam data dari serial
├── deployment_inference/       # folder sketch Arduino untuk inferensi
│   ├── deployment_inference.ino
│   ├── model.h                 # dihasilkan notebook
│   └── scaler_params.h         # dihasilkan notebook
├── docs/                       # paper IEEE & laporan teknis (PDF)
└── README.md
```

---

## ⚙️ Spesifikasi Teknis Singkat

| Komponen | Nilai |
|:---|:---|
| Window size | 150 sampel |
| Step size | 75 sampel (overlap 50%) |
| Jumlah fitur | 36 (6 statistik × 6 sumbu) |
| Arsitektur MLP | 36 → 16 → 8 → 5 (softmax) |
| Normalisasi | StandardScaler (z-score) |
| Format model | TensorFlow Lite Micro (float32) |
| Resolver | AllOpsResolver |
| Skema evaluasi | Leave-One-Subject-Out (GroupKFold) |

> **Catatan keterbatasan:** dataset bersifat skala laboratorium dengan jumlah *window* terbatas, sehingga metrik per kelas pada satu subjek uji bersifat indikatif. Evaluasi *cross-subject* disertakan sebagai ukuran generalisasi yang lebih representatif.
