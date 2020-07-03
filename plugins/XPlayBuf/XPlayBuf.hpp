// PluginXPlayBuf.hpp
// Gianluca Elia (elgiano@gmail.com)

#pragma once

#include "SC_PlugIn.hpp"

namespace XPlayBuf {

struct Loop {
  double phase = -1.;
  double start = -1.;
  double end = -1.;
  double samples = 0.;
};

class XPlayBuf;
typedef void (XPlayBuf::*FadeFunc)(int, int, float, double);

class XPlayBuf : public SCUnit {
public:
  XPlayBuf();

  // Destructor
  // ~XPlayBuf();

private:
  // Calc function
  void next(int nSamples);
  bool getBuf(int nSamples);
  void readInputs();
  void updateLoop();

  bool wrapPos(Loop &loop);
  void loopBody4(const int nSamples, const int sampleIndex, const Loop loop,
                 FadeFunc writeFunc, double mix);

  void write(int channel, int SAMPLE_INDEX, float in, double mix);
  void overwrite_equalPower(int channel, int SAMPLE_INDEX, float in,
                            double mix);
  void overwrite_lin(int channel, int SAMPLE_INDEX, float in, double mix);

  // Member variables
  Loop currLoop;
  Loop prevLoop;

  double mPlaybackRate;
  int32 mLoop;
  float m_prevtrig;
  float m_fbufnum;
  float m_failedBufNum;
  double m_fadeSamples;
  double m_rFadeSamples;
  double m_remainingFadeSamples;
  FadeFunc mFadeFunc;
  SndBuf *m_buf;
};

} // namespace XPlayBuf
