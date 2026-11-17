#include "qcs/state.h"
#include "qcs_simulator.h"
#include <gfx/gfx.h>
#include <math.h>
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

  // a (p0, p1) per workgroup of amplitudes
  size_t groups =
      (((size_t)1 << n) + QCS_GPU_WORKGROUP_SIZE - 1) / QCS_GPU_WORKGROUP_SIZE;
  GFX_StorageBuffer sums =
      gfx_storage_buffer_create(groups * 2 * sizeof(float), false);

  *out = (QCS_GPUState){
      .n = n,
      .amps = amps,
      .sums = sums,
  };
  return QCS_ERR_OK;
}

void qcs_gpu_state_destroy(QCS_GPUState *state) {
  gfx_storage_buffer_destroy(&state->amps);
  gfx_storage_buffer_destroy(&state->sums);
}

static QCS_GPUGate qcs_gpu_gate_from_cpu(QCS_Gate gate) {
  QCS_GPUGate u = {.target = gate.target,
                   .controls = gate.control_ones | gate.control_zeros,
                   .control_ones = gate.control_ones};
  for (int r = 0; r < 2; r++) {
    for (int c = 0; c < 2; c++) {
      u.m[r][c][0] = (float)creal(gate.m[r][c]);
      u.m[r][c][1] = (float)cimag(gate.m[r][c]);
    }
  }
  return u;
}

// dispatch the gates of circuit on state with gate_shader, the global phase
// on the first
static QCS_Error
qcs_gpu_circuit_dispatch(const QCS_Circuit *circuit, QCS_GPUState *state,
                         const GFX_ComputeShader *gate_shader) {
  if (circuit->qubit_count > state->n) {
    fprintf(stderr,
            "[e] invalid state: state has %u qubits, circuit is built for %u\n",
            state->n, circuit->qubit_count);
    return QCS_ERR_INVALID_STATE;
  }

  const GFX_StorageBuffer *buffers[] = {&state->amps};
  size_t pairs = ((size_t)1 << state->n) / 2;
  uint32_t gx, gy;
  gfx_compute_groups(pairs, QCS_GPU_WORKGROUP_SIZE, &gx, &gy);

  gfx_compute_begin();
  for (size_t g = 0; g < qcs_circuit_gate_count(circuit); g++) {
    // the global phase on the first gate
    QCS_Gate gate = circuit->gates[g];
    if (g == 0) {
      gate = qcs_gate_with_phase(gate, circuit->global_phase);
    }
    QCS_GPUGate gate_uniform = qcs_gpu_gate_from_cpu(gate);
    gfx_compute_dispatch(gate_shader, buffers, &gate_uniform, gx, gy, 1);
  }
  gfx_compute_end();
  return QCS_ERR_OK;
}

QCS_Error qcs_gpu_circuit_run(const QCS_Circuit *circuit, QCS_GPUState *state) {
  GFX_ComputeShader gate_shader = gfx_compute_shader_create(
      QCS_SHADER_DIR "gate.wgsl", sizeof(QCS_GPUGate), 1);
  QCS_Error err = qcs_gpu_circuit_dispatch(circuit, state, &gate_shader);
  gfx_compute_shader_destroy(&gate_shader);
  return err;
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

// qubit_probabilities.wgsl's uniform, padded to 16 bytes
typedef struct {
  uint32_t qubit;
  uint32_t pad[3];
} QCS_GPUMeasure;

// collapse.wgsl's uniform, padded to 16 bytes
typedef struct {
  uint32_t qubit, value;
  float scale; // 1/√p of the value
  uint32_t pad;
} QCS_GPUCollapse;

static_assert(sizeof(QCS_GPUMeasure) == 16);
static_assert(sizeof(QCS_GPUCollapse) == 16);

// Compiled shaders of the measures and the executor
typedef struct {
  GFX_ComputeShader gate, probabilities, collapse, copy;
} QCS_GPUShaders;

static QCS_GPUShaders qcs_gpu_shaders_create(void) {
  return (QCS_GPUShaders){
      .gate = gfx_compute_shader_create(QCS_SHADER_DIR "gate.wgsl",
                                        sizeof(QCS_GPUGate), 1),
      .probabilities = gfx_compute_shader_create(
          QCS_SHADER_DIR "qubit_probabilities.wgsl", sizeof(QCS_GPUMeasure), 2),
      .collapse = gfx_compute_shader_create(QCS_SHADER_DIR "collapse.wgsl",
                                            sizeof(QCS_GPUCollapse), 1),
      .copy = gfx_compute_shader_create(QCS_SHADER_DIR "copy.wgsl", 0, 2),
  };
}

static void qcs_gpu_shaders_destroy(QCS_GPUShaders *shaders) {
  gfx_compute_shader_destroy(&shaders->gate);
  gfx_compute_shader_destroy(&shaders->probabilities);
  gfx_compute_shader_destroy(&shaders->collapse);
  gfx_compute_shader_destroy(&shaders->copy);
}

// sum the probabilities of qubit being 0 and 1 into p: one sum per workgroup
// on the GPU, the sums added in double
static QCS_Error
qcs_gpu_state_qubit_probabilities(QCS_GPUState *state,
                                  const QCS_GPUShaders *shaders, uint32_t qubit,
                                  double p[2]) {
  if (qubit >= state->n) {
    fprintf(stderr, "[e] invalid qubit: %u, state has %u qubits\n", qubit,
            state->n);
    return QCS_ERR_QUBIT_RANGE;
  }

  // a sum per workgroup
  QCS_GPUMeasure uniform = {.qubit = qubit};
  const GFX_StorageBuffer *buffers[] = {&state->amps, &state->sums};
  uint32_t gx, gy;
  gfx_compute_groups((size_t)1 << state->n, QCS_GPU_WORKGROUP_SIZE, &gx, &gy);
  gfx_compute_begin();
  gfx_compute_dispatch(&shaders->probabilities, buffers, &uniform, gx, gy, 1);
  gfx_compute_end();

  // read them back, add them
  size_t size = state->sums.size;
  size_t groups = size / (2 * sizeof(float));
  float *sums = malloc(size);
  if (!sums) {
    fprintf(stderr, "[e] out of memory for %zu workgroup sums\n", groups);
    return QCS_ERR_OUT_OF_MEMORY;
  }
  if (gfx_storage_buffer_read(&state->sums, 0, sums, size) != GFX_ERR_OK) {
    fprintf(stderr, "[e] could not read back the qubit probabilities\n");
    free(sums);
    return QCS_ERR_GPU;
  }
  p[0] = 0;
  p[1] = 0;
  for (size_t g = 0; g < groups; g++) {
    p[0] += sums[2 * g];
    p[1] += sums[2 * g + 1];
  }
  free(sums);
  return QCS_ERR_OK;
}

// collapse qubit onto value, p the probabilities of 0 and 1
static QCS_Error qcs_gpu_state_collapse_with(QCS_GPUState *state,
                                             const QCS_GPUShaders *shaders,
                                             uint32_t qubit, bool value,
                                             const double p[2]) {
  if (!(p[value] > 0)) {
    fprintf(stderr, "[e] invalid state: q[%u] is %d with probability %.17g\n",
            qubit, value, p[value]);
    return QCS_ERR_INVALID_STATE;
  }
  QCS_GPUCollapse uniform = {
      .qubit = qubit, .value = value, .scale = (float)(1 / sqrt(p[value]))};
  const GFX_StorageBuffer *buffers[] = {&state->amps};
  uint32_t gx, gy;
  gfx_compute_groups((size_t)1 << state->n, QCS_GPU_WORKGROUP_SIZE, &gx, &gy);
  gfx_compute_begin();
  gfx_compute_dispatch(&shaders->collapse, buffers, &uniform, gx, gy, 1);
  gfx_compute_end();
  return QCS_ERR_OK;
}

// measure qubit into value: probabilities on the GPU, the draw as on the CPU,
// then collapse
static QCS_Error qcs_gpu_state_measure_with(QCS_GPUState *state,
                                            const QCS_GPUShaders *shaders,
                                            uint32_t qubit, QCS_Random *random,
                                            bool *value) {
  double p[2];
  QCS_Error err = qcs_gpu_state_qubit_probabilities(state, shaders, qubit, p);
  if (err != QCS_ERR_OK) {
    return err;
  }
  double total = p[0] + p[1];
  if (!(total > 0)) {
    fprintf(stderr, "[e] invalid state: total probability %.17g\n", total);
    return QCS_ERR_INVALID_STATE;
  }
  *value = qcs_random_next_double(random) * total < p[0] ? 0 : 1;
  return qcs_gpu_state_collapse_with(state, shaders, qubit, *value, p);
}

QCS_Error qcs_gpu_state_measure(QCS_GPUState *state, uint32_t qubit,
                                QCS_Random *random, bool *value) {
  QCS_GPUShaders shaders = qcs_gpu_shaders_create();
  QCS_Error err =
      qcs_gpu_state_measure_with(state, &shaders, qubit, random, value);
  qcs_gpu_shaders_destroy(&shaders);
  return err;
}

QCS_Error qcs_gpu_state_collapse(QCS_GPUState *state, uint32_t qubit,
                                 bool value) {
  QCS_GPUShaders shaders = qcs_gpu_shaders_create();
  double p[2];
  QCS_Error err = qcs_gpu_state_qubit_probabilities(state, &shaders, qubit, p);
  if (err == QCS_ERR_OK) {
    err = qcs_gpu_state_collapse_with(state, &shaders, qubit, value, p);
  }
  qcs_gpu_shaders_destroy(&shaders);
  return err;
}

// a GPU state of n qubits, or NULL
static void *qcs_gpu_simulator_create(void *, uint32_t n) {
  QCS_GPUState *state = malloc(sizeof *state);
  if (!state || qcs_gpu_state_create(n, state) != QCS_ERR_OK) {
    free(state);
    return nullptr;
  }
  return state;
}

// a copy of a GPU state, by the copy kernel, or NULL
static void *qcs_gpu_simulator_copy(void *context, const void *state) {
  const QCS_GPUShaders *shaders = context;
  const QCS_GPUState *from = state;
  QCS_GPUState *copy = qcs_gpu_simulator_create(context, from->n);
  if (!copy) {
    return nullptr;
  }
  const GFX_StorageBuffer *buffers[] = {&from->amps, &copy->amps};
  uint32_t gx, gy;
  gfx_compute_groups((size_t)1 << from->n, QCS_GPU_WORKGROUP_SIZE, &gx, &gy);
  gfx_compute_begin();
  gfx_compute_dispatch(&shaders->copy, buffers, NULL, gx, gy, 1);
  gfx_compute_end();
  return copy;
}

static void qcs_gpu_simulator_destroy(void *, void *state) {
  qcs_gpu_state_destroy(state);
  free(state);
}

static QCS_Error qcs_gpu_simulator_run(void *context, void *state,
                                       const QCS_Circuit *circuit) {
  const QCS_GPUShaders *shaders = context;
  return qcs_gpu_circuit_dispatch(circuit, state, &shaders->gate);
}

static QCS_Error qcs_gpu_simulator_qubit_probabilities(void *context,
                                                       void *state,
                                                       uint32_t qubit,
                                                       double p[2]) {
  return qcs_gpu_state_qubit_probabilities(state, context, qubit, p);
}

static QCS_Error qcs_gpu_simulator_collapse(void *context, void *state,
                                            uint32_t qubit, bool value,
                                            const double p[2]) {
  return qcs_gpu_state_collapse_with(state, context, qubit, value, p);
}

static QCS_Error qcs_gpu_simulator_sample(void *, void *state, size_t shots,
                                          QCS_Random *random,
                                          size_t *outcomes) {
  return qcs_gpu_state_sample(state, shots, random, outcomes);
}

QCS_Error qcs_gpu_program_run(const QCS_Program *program, size_t shots,
                              QCS_Random *random, uint64_t *clbits) {
  QCS_GPUShaders shaders = qcs_gpu_shaders_create();
  const QCS_Simulator gpu = {
      .context = &shaders,
      .create = qcs_gpu_simulator_create,
      .copy = qcs_gpu_simulator_copy,
      .destroy = qcs_gpu_simulator_destroy,
      .run = qcs_gpu_simulator_run,
      .qubit_probabilities = qcs_gpu_simulator_qubit_probabilities,
      .collapse = qcs_gpu_simulator_collapse,
      .sample = qcs_gpu_simulator_sample,
  };
  QCS_Error err =
      qcs_simulator_run_program(&gpu, program, shots, random, clbits);
  qcs_gpu_shaders_destroy(&shaders);
  return err;
}
