// Engine/Shader/Noise.hlsli
// What SpherePS and CompositePS share (ADR-035): a hash of whole numbers, and value noise on the lattice of whole numbers in three
// dimensions, summed over octaves. A seed picks one field of it out of 2^32.

// PCG's output permutation of one step of its generator: a well-mixed word of _value.
uint Hash(uint _value)
{
  const uint state = _value * 747796405u + 2891336453u;
  const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

uint HashCell(int3 _cell, uint _seed)
{
  return Hash(asuint(_cell.x) ^ Hash(asuint(_cell.y) ^ Hash(asuint(_cell.z) ^ Hash(_seed))));
}

// A hash as a number from 0 to 1.
float Unit(uint _hash)
{
  return float(_hash & 0xFFFFFFu) / 16777216.0;
}

// Value noise at _at, 0 to 1: each lattice point's hash, blended smoothly between them.
float ValueNoise(float3 _at, uint _seed)
{
  const float3 floored = floor(_at);
  const float3 within = _at - floored;
  const float3 blend = within * within * (3.0 - 2.0 * within);
  const int3 cell = int3(floored);
  const float c000 = Unit(HashCell(cell, _seed));
  const float c100 = Unit(HashCell(cell + int3(1, 0, 0), _seed));
  const float c010 = Unit(HashCell(cell + int3(0, 1, 0), _seed));
  const float c110 = Unit(HashCell(cell + int3(1, 1, 0), _seed));
  const float c001 = Unit(HashCell(cell + int3(0, 0, 1), _seed));
  const float c101 = Unit(HashCell(cell + int3(1, 0, 1), _seed));
  const float c011 = Unit(HashCell(cell + int3(0, 1, 1), _seed));
  const float c111 = Unit(HashCell(cell + int3(1, 1, 1), _seed));
  const float near = lerp(lerp(c000, c100, blend.x), lerp(c010, c110, blend.x), blend.y);
  const float far = lerp(lerp(c001, c101, blend.x), lerp(c011, c111, blend.x), blend.y);
  return lerp(near, far, blend.z);
}

// _octaves of ValueNoise, each at twice the frequency and half the weight of the one before, 0 to 1.
float Fractal(float3 _at, uint _seed, int _octaves)
{
  float sum = 0.0;
  float weight = 0.5;
  float total = 0.0;
  float3 at = _at;
  [loop] for (int octave = 0; octave < _octaves; ++octave)
  {
    sum += weight * ValueNoise(at, _seed + uint(octave));
    total += weight;
    weight *= 0.5;
    at = at * 2.03 + float3(17.0, 31.0, 47.0);
  }
  return sum / total;
}

// _vector turned by _rotation, a unit quaternion.
float3 Rotate(float4 _rotation, float3 _vector)
{
  const float3 twice = 2.0 * cross(_rotation.xyz, _vector);
  return _vector + _rotation.w * twice + cross(_rotation.xyz, twice);
}
