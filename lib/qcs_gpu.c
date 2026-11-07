#include "qcs/state.h"
#include <gfx/gfx.h>
#include <qcs/gpu.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Bytes of one amplitude, a vec2f
#define QCS_GPU_AMP_SIZE 8
// Size of the workgroup size of the gate.wgsl shader
#define QCS_GPU_WORKGROUP_SIZE 64

// Most qubits whose state fits in one buffer and one binding
static uint32_t qcs_gpu_limits_max_qubits(GFX_GraphicsLimits limits) {
  uint64_t max = limits.max_buffer_size < limits.max_storage_binding_size
                     ? limits.max_buffer_size
                     : limits.max_storage_binding_size;
  uint32_t n = 0;
  while (n < QCS_MAX_QUBITS && (uint64_t)QCS_GPU_AMP_SIZE << (n + 1) <= max) {
    n++;
  }
  return n;
}

// n qubits over what the adapter or device limits fit
static void qcs_gpu_log_too_many_qubits(uint32_t n, const char *gpu,
                                        GFX_GraphicsLimits limits) {
  fprintf(stderr,
          "[e] %u qubits don't fit on the %s, at most %u (max buffer %llu "
          "MiB, max storage binding %llu MiB)\n",
          n, gpu, qcs_gpu_limits_max_qubits(limits),
          (unsigned long long)(limits.max_buffer_size >> 20),
          (unsigned long long)(limits.max_storage_binding_size >> 20));
}

QCS_Error qcs_gpu_init(uint32_t qubit_count) {
  GFX_GraphicsLimits adapter = gfx_graphics_adapter_limits();
  if (adapter.max_buffer_size == 0) {
    fprintf(stderr, "[e] no WebGPU adapter\n");
    return QCS_ERR_GPU;
  }
  if (qubit_count > qcs_gpu_limits_max_qubits(adapter)) {
    qcs_gpu_log_too_many_qubits(qubit_count, "GPU adapter", adapter);
    return QCS_ERR_MAX_QUBITS;
  }

  // the state buffer: 2^n amplitudes
  uint64_t size = (uint64_t)QCS_GPU_AMP_SIZE << qubit_count;
  gfx_graphics_require_limits(&(GFX_GraphicsLimits){
      .max_buffer_size = size, .max_storage_binding_size = size});
  if (gfx_graphics_init() != GFX_ERR_OK) {
    return QCS_ERR_GPU;
  }
  return QCS_ERR_OK;
}

void qcs_gpu_terminate(void) { gfx_graphics_terminate(); }

uint32_t qcs_gpu_max_qubits(void) {
  return qcs_gpu_limits_max_qubits(gfx_graphics_limits());
}

QCS_Error qcs_gpu_state_create(uint32_t n, QCS_GPUState *out) {
  if (n > qcs_gpu_max_qubits()) {
    qcs_gpu_log_too_many_qubits(n, "GPU device", gfx_graphics_limits());
    return QCS_ERR_MAX_QUBITS;
  }

  size_t size = (size_t)QCS_GPU_AMP_SIZE << n;
  GFX_StorageBuffer amps = gfx_storage_buffer_create(size, false);
  float one[2] = {1.0f, 0.0f};
  gfx_storage_buffer_write(&amps, 0, one, sizeof(one));

  *out = (QCS_GPUState){
      .n = n,
      .amps = amps,
  };
  return QCS_ERR_OK;
}

void qcs_gpu_state_destroy(QCS_GPUState *state) {
  gfx_storage_buffer_destroy(&state->amps);
}

static QCS_GPUGate qcs_gpu_gate_from_cpu(QCS_Gate gate) {
  QCS_GPUGate u = {.target = gate.target, .controls = gate.controls};
  for (int r = 0; r < 2; r++) {
    for (int c = 0; c < 2; c++) {
      u.m[r][c][0] = (float)creal(gate.m[r][c]);
      u.m[r][c][1] = (float)cimag(gate.m[r][c]);
    }
  }
  return u;
}

QCS_Error qcs_gpu_circuit_run(const QCS_Circuit *circuit, QCS_GPUState *state) {
  if (state->n != circuit->qubit_count) {
    fprintf(stderr,
            "[e] invalid state: state has %u qubits, circuit is built for %u\n",
            state->n, circuit->qubit_count);
    return QCS_ERR_INVALID_STATE;
  }

  GFX_ComputeShader gate_shader = gfx_compute_shader_create(
      QCS_SHADER_DIR "gate.wgsl", sizeof(QCS_GPUGate), 1);
  const GFX_StorageBuffer *buffers[] = {&state->amps};

  size_t pairs = ((size_t)1 << state->n) / 2;
  uint32_t gx, gy;
  gfx_compute_groups(pairs, QCS_GPU_WORKGROUP_SIZE, &gx, &gy);

  gfx_compute_begin();
  for (size_t g = 0; g < qcs_circuit_gate_count(circuit); g++) {
    QCS_GPUGate gate_uniform = qcs_gpu_gate_from_cpu(circuit->gates[g]);
    gfx_compute_dispatch(&gate_shader, buffers, &gate_uniform, gx, gy, 1);
  }
  gfx_compute_end();

  gfx_compute_shader_destroy(&gate_shader);
  return QCS_ERR_OK;
}

// GPU state as a CPU state, with floats widened to doubles
QCS_Error qcs_gpu_state_amplitudes(const QCS_GPUState *state, QCS_State *out) {
  QCS_Error err = qcs_state_create(state->n, out);
  if (err != QCS_ERR_OK) {
    return err;
  }

  size_t size = qcs_state_size(out);
  float *amps = calloc(2 * size, sizeof(float));
  if (!amps) {
    fprintf(stderr,
            "[e] out of memory while allocating amplitudes of size %zu\n",
            size);
    qcs_state_destroy(out);
    return QCS_ERR_OUT_OF_MEMORY;
  }
  if (gfx_storage_buffer_read(&state->amps, 0, amps,
                              2 * size * sizeof(float)) != GFX_ERR_OK) {
    free(amps);
    qcs_state_destroy(out);
    fprintf(stderr, "[e] could not read back GPU state storage buffer\n");
    return QCS_ERR_GPU;
  }
  for (size_t i = 0; i < size; i++) {
    out->amps[i] = amps[2 * i] + I * amps[2 * i + 1];
  }

  free(amps);
  return QCS_ERR_OK;
}

// Probability |a|^2 of every amplitude of the state
QCS_Error qcs_gpu_state_probabilities(const QCS_GPUState *state, float *out) {
  GFX_ComputeShader probs_shader =
      gfx_compute_shader_create(QCS_SHADER_DIR "probabilities.wgsl", 0, 2);

  size_t probs_size = (size_t)4 << state->n;
  GFX_StorageBuffer probs =
      gfx_storage_buffer_create(probs_size, false); // Half the state size
  const GFX_StorageBuffer *buffers[] = {&state->amps, &probs};

  uint32_t gx, gy;
  gfx_compute_groups((size_t)1 << state->n, QCS_GPU_WORKGROUP_SIZE, &gx, &gy);
  gfx_compute_begin();
  gfx_compute_dispatch(&probs_shader, buffers, NULL, gx, gy, 1);
  gfx_compute_end();

  if (gfx_storage_buffer_read(&probs, 0, out, probs_size) != GFX_ERR_OK) {
    fprintf(stderr, "[e] could not read back probabilities storage buffer\n");
    gfx_storage_buffer_destroy(&probs);
    gfx_compute_shader_destroy(&probs_shader);
    return QCS_ERR_GPU;
  }

  gfx_storage_buffer_destroy(&probs);
  gfx_compute_shader_destroy(&probs_shader);
  return QCS_ERR_OK;
}

QCS_Error qcs_gpu_state_sample(const QCS_GPUState *state, size_t shots,
                               QCS_Random *random, size_t *outcomes) {
  size_t size = (size_t)1 << state->n;
  float *probs = malloc(size * sizeof(float));
  double *cdf = malloc(size * sizeof(double));
  if (!probs || !cdf) {
    fprintf(stderr,
            "[e] out of memory while allocating the distribution of %zu "
            "patterns\n",
            size);
    free(probs);
    free(cdf);
    return QCS_ERR_OUT_OF_MEMORY;
  }
  QCS_Error err = qcs_gpu_state_probabilities(state, probs);
  if (err != QCS_ERR_OK) {
    free(probs);
    free(cdf);
    return err;
  }

  // cumulative distribution, summed in double
  double total = 0;
  for (size_t i = 0; i < size; i++) {
    total += probs[i];
    cdf[i] = total;
  }
  free(probs);
  if (!(total > 0)) {
    fprintf(stderr, "[e] invalid state: total probability %.17g\n", total);
    free(cdf);
    return QCS_ERR_INVALID_STATE;
  }

  qcs_sample_cdf(cdf, size, shots, random, outcomes);
  free(cdf);
  return QCS_ERR_OK;
}
