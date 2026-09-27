/*
 * raf_config.h
 *
 *  Created on: 19 Sept 2026
 *      Author: Rafdi
 */

 #ifndef MAIN_INCLUDE_RAF_CONFIG_H_
 #define MAIN_INCLUDE_RAF_CONFIG_H_

 /*
  * RAF Framework Configuration
  * ===========================
  *
  * Semua konfigurasi framework terpusat di file ini.
  * User bisa override dengan 2 cara:
  *
  *   1. Edit nilai langsung di bawah (paling mudah).
  *   2. Override via build flag:
  *        idf.py -DRAF_CONFIG_MAX_TASKS=32 build
  *
  *      Atau di CMakeLists.txt:
  *        target_compile_definitions(${COMPONENT_LIB} PRIVATE
  *            RAF_CONFIG_MAX_TASKS=32
  *            RAF_CONFIG_WDT_TIMEOUT_MS=8000)
  */

 /* ============================================================
  * TASK CONFIGURATION
  * ============================================================ */

 /**
  * @brief Jumlah maksimum task yang bisa didaftarkan.
  *
  * Setiap task memakan: ~100 byte RAM (slot struct) + stack_size (default 3072).
  * Task 16 × 3072 byte = ~50 KB RAM. Pastikan heap cukup.
  *
  * Range aman: 4 - 32.
  */
 #ifndef RAF_CONFIG_MAX_TASKS
 #define RAF_CONFIG_MAX_TASKS 32
 #endif

 /**
  * @brief Panjang maksimum nama task (termasuk null terminator).
  *
  * Range aman: 8 - 32.
  */
 #ifndef RAF_CONFIG_TASK_NAME_MAX_LEN
 #define RAF_CONFIG_TASK_NAME_MAX_LEN 16
 #endif

 /* ============================================================
  * WATCHDOG CONFIGURATION
  * ============================================================ */

 /**
  * @brief Aktifkan task watchdog (1 = aktif, 0 = nonaktif).
  *
  * Recommended: 1.
  * Nonaktifkan hanya untuk debugging atau jika ada task yang
  * memang tidak boleh di-reset oleh WDT (rare).
  */
 #ifndef RAF_CONFIG_WDT_ENABLED
 #define RAF_CONFIG_WDT_ENABLED 1
 #endif

 /**
  * @brief WDT timeout default (ms).
  *
  * Nilai ini menjadi MINIMUM. Jika auto-expand aktif (lihat bawah)
  * dan ada task dengan period lebih lama, WDT akan otomatis
  * diperpanjang.
  *
  * Range aman: 1000 - 30000 ms.
  */
 #ifndef RAF_CONFIG_WDT_TIMEOUT_MS
 #define RAF_CONFIG_WDT_TIMEOUT_MS 12000   	
 #endif

 /**
  * @brief Auto-expand WDT jika ada task dengan period panjang.
  *
  * Jika 1 (default):
  *   WDT timeout = MAX(RAF_CONFIG_WDT_TIMEOUT_MS, max_period + grace)
  *
  * Jika 0:
  *   WDT timeout = RAF_CONFIG_WDT_TIMEOUT_MS (fixed)
  *   User bertanggung jawab jika ada task dengan period > timeout.
  *   Framework akan log warning jika mendeteksi hal ini.
  */
 #ifndef RAF_CONFIG_WDT_AUTO_EXPAND
 #define RAF_CONFIG_WDT_AUTO_EXPAND 1
 #endif

 /**
  * @brief Grace margin untuk auto-expand (ms).
  *
  * WDT timeout = max_period + margin saat auto-expand aktif.
  * Margin harus cukup untuk satu iterasi task yang paling lambat.
  */
 #ifndef RAF_CONFIG_WDT_GRACE_MARGIN_MS
 #define RAF_CONFIG_WDT_GRACE_MARGIN_MS 3000
 #endif

 /* ============================================================
  * COMPILE-TIME VALIDATION
  * ============================================================ */

 #if RAF_CONFIG_MAX_TASKS < 1 || RAF_CONFIG_MAX_TASKS > 64
 #error "RAF_CONFIG_MAX_TASKS harus antara 1 dan 64"
 #endif

 #if RAF_CONFIG_TASK_NAME_MAX_LEN < 8 || RAF_CONFIG_TASK_NAME_MAX_LEN > 64
 #error "RAF_CONFIG_TASK_NAME_MAX_LEN harus antara 8 dan 64"
 #endif

 #if RAF_CONFIG_WDT_TIMEOUT_MS < 100
 #error "RAF_CONFIG_WDT_TIMEOUT_MS minimal 100 ms"
 #endif

 #if RAF_CONFIG_WDT_GRACE_MARGIN_MS < 100
 #error "RAF_CONFIG_WDT_GRACE_MARGIN_MS minimal 100 ms"
 #endif

 #endif /* MAIN_INCLUDE_RAF_CONFIG_H_ */


