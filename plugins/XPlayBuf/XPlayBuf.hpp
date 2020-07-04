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
typedef void (XPlayBuf::*FadeFunc)(const int &, const int &, const float &,
                                   const double &);

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
  double getFadeAtBounds(const Loop &loop);
  void loopBody4(const int &nSamples, const int &outSample, const Loop &loop,
                 FadeFunc writeFunc, double mix);

  void write(const int &channel, const int &OUT_SAMPLE, const float &in,
             const double &mix);
  void overwrite_equalPower(const int &channel, const int &OUT_SAMPLE,
                            const float &in, const double &mix);
  void overwrite_lin(const int &channel, const int &OUT_SAMPLE, const float &in,
                     const double &mix);

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
