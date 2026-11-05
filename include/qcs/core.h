#ifndef QCS_CORE_DECL
#define QCS_CORE_DECL

#include <complex.h>
#include <stddef.h>
#include <stdint.h>

#define QCS_MAX_QUBITS 32

typedef enum : uint8_t {
  QCS_ERR_OK,
  QCS_ERR_OUT_OF_MEMORY,
  QCS_ERR_MAX_QUBITS,
  QCS_ERR_CONTROL_ON_TARGET,
  QCS_ERR_QUBIT_RANGE,
  QCS_ERR_INVALID_STATE,
} QCS_Error;

QCS_Error qcs_bitstring(uint32_t n, size_t i, char *out);

bool qcs_eq(double a, double b);
bool qcs_ceq(double complex a, double complex b);

#endif
