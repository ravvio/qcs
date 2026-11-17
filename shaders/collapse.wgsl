// Keeps the amplitudes where a qubit has the measured value, scaled, and
// zeroes the others

const workgroup_size = 64u;

struct Collapse {
    // Measured qubit and its value, 0 or 1
    qubit: u32,
    value: u32,
    // 1/√p of the value
    scale: f32,
}

@group(0) @binding(0) var<uniform> collapse: Collapse;
@group(0) @binding(1) var<storage, read_write> state: array<vec2f>;

@compute @workgroup_size(workgroup_size)
fn comp_main(@builtin(workgroup_id) group: vec3u,
             @builtin(num_workgroups) groups: vec3u,
             @builtin(local_invocation_index) local: u32) {
    let i = (group.y * groups.x + group.x) * workgroup_size + local;
    if (i >= arrayLength(&state)) {
        return;
    }
    let bit = (i >> collapse.qubit) & 1u;
    state[i] = select(vec2f(0.0), state[i] * collapse.scale,
                      bit == collapse.value);
}
