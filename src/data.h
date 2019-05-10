#include <vector>
#include <iostream>
#include <sstream>
#include <string>

#define ACCELEROMETER 0
#define GYROSCOPE 1

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
  int _type;
  unsigned long long timestamp; // timestamp of first data
  size_t _tick, _nextIdx; // tick: deviceSamplingPeriod, nextIdx: samplingPeriod
  bool _done;
  float data[C][D * 1000 / _samplingPeriod];

  Measure(int id, int type)
    : _id(id), _type(type), _tick(0), _nextIdx(0), _done(false) {}

  constexpr size_t _size() {
    return 4 + C * D * 1000 / _samplingPeriod;
  }

  size_t _numSamples() const
  {
    return _nextIdx;
  }

  std::string format()
  {
    int numSample = _numSamples();
    std::string sensor_type = "";

    // Check the Measure type (accel or gyro)
    switch (_type) {
	    case ACCELEROMETER:
	    	sensor_type = "accel";
	    	break;
	    case GYROSCOPE:
	    	sensor_type = "gyro";
	    	break;
	    default:
	    	break;
	 }

	 // JSON formatting
	 std::string timestamps = std::to_string(timestamp);
	 std::string data[C] = {"", "", ""}; // data[0] = "10.0, 5.0, 2.0, ..."

	 for (unsigned int i = 0; i < C; i++) {
	   for (int j = 0; j < numSample; j++) {
	     data[i] += std::to_string(this->data[i][j]);
	     if (j < numSample-1)
	       data[i] += ",";
	   }
	 }

	 std::string jsonObj = "{\"timestamps\":" + timestamps + ",\"" + sensor_type + "\":{\"x\":[" + data[0] + "],\"y\":[" + data[1] + "],\"z\":[" + data[2] + "]}}";
	 return jsonObj;
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

    // Store data
    for (unsigned int i = 0; i < _numChannels(); i++) {
      data[i][idx] = readValue[i];
    }

    if (_nextIdx == (_duration() * 1000 / _samplingPeriod)) {
      _done = true;
    }
  }

  void setTimestamp(unsigned long long timestamp)
  {
  	this->timestamp = timestamp;
  }
};
