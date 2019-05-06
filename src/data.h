#include <vector>
#include <string>

//
// This stores data from sensor with `C` channels for `D` seconds.
// TODO: Preprocessing?
//
template <std::size_t C, std::size_t D>
struct Measure {
  static const int _samplingPeriod = 40;       // ms
  static const int _deviceSamplingPeriod = 10; // ms

  constexpr std::size_t _numChannels() { return C; }
  constexpr std::size_t _duration() { return D; }

  int _id;
  size_t _tick, _nextIdx; // tick: deviceSamplingPeriod, nextIdx: samplingPeriod
  bool _done;
  float data[C][D * 1000 / _samplingPeriod];

  // TODO: assign id
  Measure() : _id(0), _tick(0), _nextIdx(0), _done(false) {}

  constexpr size_t _size() {
    return 4 + C * D * 1000 / _samplingPeriod;
  }

  size_t _numSamples() const
  {
    return _nextIdx;
  }

  std::vector<uint8_t> format()
  {
    return std::vector<uint8_t>(3);
  }

  std::vector<float> & operator[](std::size_t idx)
  {
    return data[idx];
  }

  const std::vector<float>& operator[](std::size_t idx) const
  {
    return data[idx];
  }

  void tick(const std::vector<float>& readValue)
  {
    if (_done)
      return;

    _tick++;
    if (_tick % (_samplingPeriod / _deviceSamplingPeriod)) {
      return;
    }
    size_t idx = _nextIdx++;
    if (readValue.size() != _numChannels())
      return;

    for (int i = 0; i < _numChannels(); i++) {
      data[i][idx] = readValue[i];
    }

    if (_nextIdx == (_duration() * 1000 / _samplingPeriod)) {
      _done = true;
    }
  }
};
