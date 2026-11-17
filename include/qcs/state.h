#ifndef QCS_STATE_DECL
#define QCS_STATE_DECL

#include <complex.h>
#include <qcs/core.h>
#include <qcs/gate.h>
#include <qcs/random.h>
#include <stddef.h>
#include <stdint.h>

// The state vector of n qubits (2^n patterns)
//
// |amps[i]|^2 = probability of measuring pattern i
typedef struct {
  uint32_t n;
  double complex *amps; // 2^n
} QCS_State;

QCS_Error qcs_state_create(uint32_t n, QCS_State *out);
QCS_Error qcs_state_create_random(uint32_t n, QCS_Random *random,
                                  QCS_State *out);
// The basis state |index⟩, the qubits set to the bits of index
QCS_Error qcs_state_create_basis(uint32_t n, size_t index, QCS_State *out);
void qcs_state_destroy(QCS_State *state);
void qcs_state_apply_gate(QCS_State *state, QCS_Gate gate);

// Number of amplitudes, 2^n
size_t qcs_state_size(const QCS_State *state);
// Probability of measuring pattern index, |amps[index]|^2
double qcs_state_probability(const QCS_State *state, size_t index);
// Sum of all probabilities, 1 for a valid state
double qcs_state_norm(const QCS_State *state);

// Measure all qubits shots times, the pattern of each shot in outcomes
QCS_Error qcs_state_sample(const QCS_State *state, size_t shots,
                           QCS_Random *random, size_t *outcomes);
// shots patterns drawn from cdf, the cumulative probabilities of size
// patterns, into outcomes
void qcs_sample_cdf(const double *cdf, size_t size, size_t shots,
                    QCS_Random *random, size_t *outcomes);
// Calculate fidelity of state a with respect to b
QCS_Error qcs_state_fidelity(const QCS_State *a, const QCS_State *b,
                             double *out);

// Measure qubit into value, collapsing the state
QCS_Error qcs_state_measure(QCS_State *state, uint32_t qubit,
                            QCS_Random *random, bool *value);
// Measure qubit and set it to 0
QCS_Error qcs_state_reset(QCS_State *state, uint32_t qubit, QCS_Random *random);

#endif
