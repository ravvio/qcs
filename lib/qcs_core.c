#include <complex.h>
#include <math.h>
#include <qcs/core.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Tolerances of f32 and f64 comparisons
#define QCS_EPS_F32 1e-6f
#define QCS_EPS_F64 1e-12

QCS_Error qcs_bitstring(uint32_t n, size_t i, char *out) {
  if (n > QCS_MAX_QUBITS) {
    fprintf(stderr, "[e] qubits number %u is over max qubits %u\n", n,
            QCS_MAX_QUBITS);
    return QCS_ERR_MAX_QUBITS;
  }

  size_t size = (size_t)1 << n;
  if (i >= size) {
    fprintf(stderr, "[e] index %zu is over state size %zu\n", i, size);
    return QCS_ERR_QUBIT_RANGE;
  }

  for (uint32_t k = 0; k < n; k++) {
    size_t mask = (size_t)1 << (n - 1 - k);
    out[k] = (i & mask) ? '1' : '0';
  }
  out[n] = '\0';

  return QCS_ERR_OK;
}

bool qcs_f32_near(float a, float b) { return fabsf(a - b) < QCS_EPS_F32; }

bool qcs_f64_near(double a, double b) { return fabs(a - b) < QCS_EPS_F64; }

bool qcs_c64_near(float complex a, float complex b) {
  return cabsf(a - b) < QCS_EPS_F32;
}

bool qcs_c128_near(double complex a, double complex b) {
  return cabs(a - b) < QCS_EPS_F64;
}
