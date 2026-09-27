/*
 * kalman_filter.c
 *
 *  Created on: 23 Sept 2026
 *      Author: Rafdi
 */
 #include "kalman_filter.h"

 void kalman1d_init(kalman1d_t *kf, float Q, float R, float initial_value) {
     if (kf == NULL) return;
     kf->Q = Q;
     kf->R = R;
     kf->P = 1.0f;
     kf->x = initial_value;
     kf->initialized = true;
 }

 void kalman1d_reset(kalman1d_t *kf) {
     if (kf == NULL) return;
     kf->P = 1.0f;
     kf->initialized = false;
 }

 float kalman1d_update(kalman1d_t *kf, float measurement) {
     if (kf == NULL) return measurement;

     if (!kf->initialized) {
         kf->x = measurement;
         kf->P = 1.0f;
         kf->initialized = true;
         return measurement;
     }

     // Predict
     kf->P = kf->P + kf->Q;

     // Update
     float K = kf->P / (kf->P + kf->R);
     kf->x = kf->x + K * (measurement - kf->x);
     kf->P = (1.0f - K) * kf->P;

     return kf->x;
 }



