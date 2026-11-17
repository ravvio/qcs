// Simulator stress test in OpenQASM 3.0
// Mirror circuit: random-looking layers, then their exact inverse.
// A correct simulator must return all zeros on every shot.
// Statevector memory is 16 * 2^N bytes (N = 28 is about 4 GB, N = 30 about 16 GB).

OPENQASM 3.0;
include "stdgates.inc";

const int N = 24;     // qubits: raise to stress memory
const int DEPTH = 5;  // layers: raise to stress runtime

qubit[N] q;
bit[N] c;

// Forward half
for int d in [0:DEPTH-1] {
    for int i in [0:N-1] {
        ry(0.1 * (d + 1) + 0.37 * i) q[i];
        rz(0.23 * d + 0.71 * i) q[i];
    }
    for int i in [0:2:N-2] { cx q[i], q[i+1]; }
    for int i in [1:2:N-2] { cx q[i], q[i+1]; }
}

barrier q;

// Inverse half: same layers in reverse order, gates reversed, angles negated
for int d in [DEPTH-1:-1:0] {
    for int i in [1:2:N-2] { cx q[i], q[i+1]; }
    for int i in [0:2:N-2] { cx q[i], q[i+1]; }
    for int i in [0:N-1] {
        rz(-(0.23 * d + 0.71 * i)) q[i];
        ry(-(0.1 * (d + 1) + 0.37 * i)) q[i];
    }
}

// Expected: 000...0 with probability 1
c = measure q;
