// Applies one gate to the state

const workgroup_size = 64u;

struct Gate {
    // Complex 2x2 matrix members
    m00: vec2f,
    m01: vec2f,
    m10: vec2f,
    m11: vec2f,
    // Target qubit
    target_qubit: u32,
    // Control qubits bitmask
    controls: u32,
}

@group(0) @binding(0) var<uniform> gate: Gate;
@group(0) @binding(1) var<storage, read_write> state: array<vec2f>;

fn cmul(a: vec2f, b: vec2f) -> vec2f {
    let re = a.x * b.x - a.y * b.y;
    let im = a.x * b.y + a.y * b.x;
    return vec2f(re, im);
}

@compute @workgroup_size(workgroup_size)
fn comp_main(@builtin(workgroup_id) group: vec3u,
             @builtin(num_workgroups) groups: vec3u,
             @builtin(local_invocation_index) local: u32) {
    let pair_count = arrayLength(&state) / 2;
    // pair without target
    let p = (group.y * groups.x + group.x) * workgroup_size + local;
    if (p >= pair_count) {
        return;
    }

    // insert 0/1 at target bit and retrieve amplitude pair indexes
    let target_bit = 1u << gate.target_qubit;
    let target_mask = target_bit - 1;
    let low = (p & target_mask);
    let high = (p & ~target_mask) << 1;
    let i0 = high | low;
    let i1 = i0 | target_bit;

    // check controls
    if ((i0 & gate.controls) != gate.controls) {
        return;
    }

    // retrieve amplitudes
    let a0 = state[i0];
    let a1 = state[i1];
    // apply gate: m * v
    state[i0] = cmul(gate.m00, a0) + cmul(gate.m01, a1);
    state[i1] = cmul(gate.m10, a0) + cmul(gate.m11, a1);
}
