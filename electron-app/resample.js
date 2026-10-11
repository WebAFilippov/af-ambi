// resample.js - map capture-zone colours onto an arbitrary LED count
//
// capture.exe emits one averaged colour per perimeter zone (the count depends
// on top/right/bottom/left in the UI). The physical strip has its own LED
// count, set independently. Both are laid out in the same order (start corner,
// direction), so the two are resampled by linear interpolation along the
// perimeter. This is a one-dimensional resample of a wrapped colour sequence.

'use strict';

// Resample a flat [r,g,b, ...] array of `zones` colours to `n` LEDs.
// Returns a new flat array of length n*3. Handles n < zones and n > zones.
function resampleZones(zones, n) {
  const zCount = zones.length / 3;
  if (zCount === 0) return new Array(n * 3).fill(0);

  const out = new Array(n * 3);
  for (let i = 0; i < n; i++) {
    // Position along the perimeter as a float in zone space.
    const pos = (i * zCount) / n;
    const i0 = Math.floor(pos);
    const i1 = (i0 + 1) % zCount;
    const frac = pos - i0;

    out[i * 3 + 0] = lerp(zones[i0 * 3 + 0], zones[i1 * 3 + 0], frac);
    out[i * 3 + 1] = lerp(zones[i0 * 3 + 1], zones[i1 * 3 + 1], frac);
    out[i * 3 + 2] = lerp(zones[i0 * 3 + 2], zones[i1 * 3 + 2], frac);
  }
  return out;
}

function lerp(a, b, t) {
  return Math.round(a + (b - a) * t);
}

module.exports = { resampleZones };
