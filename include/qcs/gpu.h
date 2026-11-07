#ifndef QCS_GPU_DECL
#define QCS_GPU_DECL

#include <gfx/compute.h>
#include <qcs/circuit.h>
#include <qcs/core.h>
#include <stddef.h>
#include <stdint.h>

#define QCS_GPU_WORKGROUP_SIZE 64

// Start the GPU with room for a state of qubit_count qubits
QCS_Error qcs_gpu_init(uint32_t qubit_count);
void qcs_gpu_terminate(void);
// Most qubits a state can have on the device, after qcs_gpu_init
uint32_t qcs_gpu_max_qubits(void);

// QCS_Gate as read by the GPU
typedef struct {
  float m[2][2][2]; // [row][col][re, im]
  uint32_t target;
  uint32_t controls;
} QCS_GPUGate;

// Make sure the type has the same size and offsets on the GPU
static_assert(sizeof(QCS_GPUGate) == 40);
static_assert(offsetof(QCS_GPUGate, target) == 32);
static_assert(offsetof(QCS_GPUGate, controls) == 36);

// QCS_State as read by the GPU
typedef struct {
  uint32_t n;
  GFX_StorageBuffer amps; // 2^n vec2f
} QCS_GPUState;

QCS_Error qcs_gpu_state_create(uint32_t n, QCS_GPUState *out);
void qcs_gpu_state_destroy(QCS_GPUState *state);

QCS_Error qcs_gpu_circuit_run(const QCS_Circuit *circuit, QCS_GPUState *state);

// GPU state as a CPU state, with floats widened to doubles
QCS_Error qcs_gpu_state_amplitudes(const QCS_GPUState *state, QCS_State *out);
// Probability |a|^2 of every amplitude of the state
QCS_Error qcs_gpu_state_probabilities(const QCS_GPUState *state, float *out);
// Measure all qubits shots times, the pattern of each shot in outcomes
QCS_Error qcs_gpu_state_sample(const QCS_GPUState *state, size_t shots,
                               QCS_Random *random, size_t *outcomes);

#endif
