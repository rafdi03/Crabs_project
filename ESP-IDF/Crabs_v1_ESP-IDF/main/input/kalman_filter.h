/*
 * kalman_filter.h
 *
 *  Created on: 23 Sept 2026
 *      Author: Rafdi
 */

 #ifndef MAIN_INCLUDE_KALMAN_FILTER_H_
 #define MAIN_INCLUDE_KALMAN_FILTER_H_

 #include <stdbool.h>
 #include <stddef.h>
 typedef struct {
     float Q;            // process noise covariance
     float R;            // measurement noise covariance
     float P;            // estimation error covariance
     float x;            // estimated value
     bool  initialized;
 } kalman1d_t;

 /**
  * @brief Inisialisasi filter. Panggil sekali saat init sensor.
  * @param Q  Process noise. Kecil = percaya model, besar = responsif.
  * @param R  Measurement noise. Kecil = percaya sensor, besar = halus.
  */
 void  kalman1d_init(kalman1d_t *kf, float Q, float R, float initial_value);

 /** @brief Reset filter (mis. saat deteksi outlier ekstrem). */
 void  kalman1d_reset(kalman1d_t *kf);

 /** @brief Masukkan measurement baru, dapatkan estimasi terfilter. */
 float kalman1d_update(kalman1d_t *kf, float measurement);

 #endif
