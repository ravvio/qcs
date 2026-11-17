#ifndef QCS_CORE_DECL
#define QCS_CORE_DECL

#include <complex.h>
#include <stddef.h>
#include <stdint.h>

#define QCS_MAX_QUBITS 32
#define QCS_MAX_CLBITS 64
#define QCS_PI 3.14159265358979323846

typedef enum : uint8_t {
  QCS_ERR_OK,
  QCS_ERR_OUT_OF_MEMORY,
  QCS_ERR_MAX_QUBITS,
  QCS_ERR_MAX_BITS,
  QCS_ERR_CONTROL_ON_TARGET,
  QCS_ERR_CONTROL_ONE_AND_ZERO,
  QCS_ERR_QUBIT_RANGE,
  QCS_ERR_INVALID_STATE,
  QCS_ERR_GPU,
  QCS_ERR_QASM_PARSE_FAILURE,
} QCS_Error;

QCS_Error qcs_bitstring(uint32_t n, size_t i, char *out);

// a and b equal up to the type's precision: 1e-6 for f32, 1e-12 for f64
bool qcs_f32_near(float a, float b);
bool qcs_f64_near(double a, double b);
// complex of two f32 / two f64
bool qcs_c64_near(float complex a, float complex b);
bool qcs_c128_near(double complex a, double complex b);

#endif
