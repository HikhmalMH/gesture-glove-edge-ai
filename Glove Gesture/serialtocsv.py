#!/usr/bin/env python3
"""
============================================================
 PEREKAM DATA GESTUR MULTI-LABEL  (Serial ESP32 -> CSV)
------------------------------------------------------------
 Satu file CSV bisa berisi BANYAK gestur sekaligus.
 Tiap baris data punya kolom 'label' sesuai gestur aktif.

 Cara pakai:
   python serialtocsv.py            (subjek default: subjek1)
   python serialtocsv.py budi       (subjek: budi)

 Alur:
   1. Run -> diminta pilih gestur awal (angka 1-5).
   2. Langsung merekam. Data tampil live di terminal.
   3. Mau ganti gestur? Ketik angka (1-5) + Enter.
       -> log auto-PAUSE, posisikan tangan, tekan Enter buat lanjut.
   4. Ctrl+C -> berhenti & SIMPAN semua gestur ke SATU file CSV.

 Peta gestur:
   1=idle  2=wave  3=rotate  4=push  5=lift

 Output:
   D:\\VSCode Project\\PMA\\data\\rekaman_<subjek>_<timestamp>.csv

 Dependensi:  pip install pyserial
============================================================
"""

import serial
import serial.tools.list_ports
import os
import sys
import time
import threading
import queue
from datetime import datetime

# ================== KONFIGURASI ==================
BAUD_RATE = 115200
DATA_DIR  = r"./data"  
GESTURE_MAP = {
    "1": "idle",
    "2": "wave",
    "3": "rotate",
    "4": "push",
    "5": "lift",
}
# =================================================

# State bersama antar-thread
state = {
    "label": "idle",      # gestur aktif sekarang
    "recording": True,    # True=ngerekam, False=pause
    "running": True,      # False=keluar program
}
state_lock = threading.Lock()
input_queue = queue.Queue()


def pick_port():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("[X] Tidak ada port serial. Hubungkan ESP32 dulu.")
        sys.exit(1)
    if len(ports) == 1:
        print(f"[i] Auto-pilih port: {ports[0].device} ({ports[0].description})")
        return ports[0].device
    print("Port serial tersedia:")
    for i, p in enumerate(ports):
        print(f"  [{i}] {p.device}  - {p.description}")
    idx = input("Pilih nomor port (atau ketik nama, mis COM11): ").strip()
    if idx.isdigit() and int(idx) < len(ports):
        return ports[int(idx)].device
    return idx


def input_thread():
    """Thread terpisah: baca input keyboard tanpa blok pembacaan serial."""
    while True:
        with state_lock:
            if not state["running"]:
                break
        try:
            line = input()
        except (EOFError, KeyboardInterrupt):
            break
        input_queue.put(line.strip())


def print_menu():
    print("\nGESTUR:  1=idle  2=wave  3=rotate  4=push  5=lift")
    print("Ketik angka + Enter buat ganti gestur. Ctrl+C buat simpan & keluar.\n")


def main():
    subject = sys.argv[1].strip().lower() if len(sys.argv) >= 2 else "subjek1"

    # ---------- Pilih gestur awal ----------
    print("=" * 60)
    print("  PEREKAM DATA GESTUR MULTI-LABEL")
    print("=" * 60)
    print_menu()
    first = ""
    while first not in GESTURE_MAP:
        first = input("Pilih gestur AWAL (1-5): ").strip()
        if first not in GESTURE_MAP:
            print("  [!] Pilih angka 1-5.")
    state["label"] = GESTURE_MAP[first]

    # ---------- Siapkan file ----------
    os.makedirs(DATA_DIR, exist_ok=True)
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    path = os.path.join(DATA_DIR, f"rekaman_{subject}_{ts}.csv")

    # ---------- Buka serial ----------
    port = pick_port()
    try:
        ser = serial.Serial(port, BAUD_RATE, timeout=1)
    except Exception as e:
        print(f"[X] Gagal membuka {port}: {e}")
        print("    Pastikan Serial Monitor Arduino TERTUTUP.")
        sys.exit(1)

    time.sleep(2)
    ser.reset_input_buffer()
    ser.write(f"label {state['label']}\n".encode())  # [FIX] set gestur awal di ESP32 -> OLED
    time.sleep(0.1)
    ser.write(b"start\n")    # picu firmware mulai kirim data

    print("=" * 60)
    print(f"  MEREKAM (subjek='{subject}')  ->  {path}")
    print(f"  Gestur awal: {state['label']}")
    print("  Ketik angka 1-5 buat ganti gestur. Ctrl+C buat simpan.")
    print("=" * 60)

    # ---------- Mulai thread input keyboard ----------
    t = threading.Thread(target=input_thread, daemon=True)
    t.start()

    buffer = []          # nampung baris full: P1,S1,R1,label,timestamp,ax,ay,az,gx,gy,gz
    count_per_label = {g: 0 for g in GESTURE_MAP.values()}

    try:
        while True:
            # ----- Cek apakah user ngetik sesuatu (ganti gestur) -----
            try:
                cmd = input_queue.get_nowait()
            except queue.Empty:
                cmd = None

            if cmd is not None:
                if cmd in GESTURE_MAP:
                    new_label = GESTURE_MAP[cmd]
                    # AUTO-PAUSE: berhenti ngerekam, tunggu user siap
                    print(f"\n[PAUSE] Ganti ke '{new_label}'. "
                          f"Posisikan tangan, lalu tekan Enter buat lanjut...")
                    ser.reset_input_buffer()   # buang data transisi
                    # tunggu Enter dari user
                    while True:
                        try:
                            _ = input_queue.get(timeout=0.1)
                            break
                        except queue.Empty:
                            continue
                    state["label"] = new_label
                    ser.write(f"label {new_label}\n".encode())  # [FIX] kabari ESP32 -> OLED update
                    ser.reset_input_buffer()   # buang lagi biar bersih
                    print(f"[REC] Lanjut merekam '{new_label}'.\n")
                elif cmd == "":
                    pass  # Enter kosong, abaikan
                else:
                    print(f"  [!] '{cmd}' bukan angka gestur (1-5). Diabaikan.")

            # ----- Baca serial -----
            raw = ser.readline().decode("utf-8", errors="ignore").strip()
            if not raw:
                continue
            
            # Abaikan pesan sistem dari ESP32
            if raw.startswith("#") or raw.startswith("=") or raw.startswith("-") \
               or "WHO_AM_I" in raw or "PERINTAH" in raw or "participant_id" in raw \
               or "Gestur" in raw or "siap" in raw or "Inisialisasi" in raw:
                continue

            parts = raw.split(",")
            
            # Validasi format 11 kolom (P1,S1,R1,label,timestamp,ax,ay,az,gx,gy,gz)
            if len(parts) == 11:
                data_sensor = parts[5:] # Ambil ax sampai gz aja buat di-print ke terminal
            else:
                continue

            try:
                vals = [float(x) for x in data_sensor]
            except ValueError:
                continue

            # Simpan RAW data langsung (karena ESP32 udah nyiapin label dll)
            lbl = state["label"]
            buffer.append(raw)
            count_per_label[lbl] += 1

            print(f"[{lbl:6s}] ax={vals[0]:7.3f} ay={vals[1]:7.3f} az={vals[2]:7.3f} "
                  f"gx={vals[3]:7.1f} gy={vals[4]:7.1f} gz={vals[5]:7.1f}", end="\r")

    except KeyboardInterrupt:
        with state_lock:
            state["running"] = False
        try:
            ser.write(b"stop\n")
        except Exception:
            pass

        print("\n" + "=" * 60)
        total = sum(count_per_label.values())
        if total == 0:
            print("[!] Tidak ada data. File TIDAK disimpan.")
        else:
            with open(path, "w") as f:
                # Update Header CSV sesuai 11 kolom
                f.write("participant_id,session_id,repetition_id,label,timestamp_ms,ax,ay,az,gx,gy,gz\n")
                f.write("\n".join(buffer) + "\n")
            print(f"[SAVED] {path}")
            print(f"        Total {total} sampel. Rincian per gestur:")
            for g, c in count_per_label.items():
                if c > 0:
                    print(f"          {g:8s}: {c:5d} sampel (~{c/100:.1f} dtk)")
        print("=" * 60)
    finally:
        ser.close()


if __name__ == "__main__":
    main()