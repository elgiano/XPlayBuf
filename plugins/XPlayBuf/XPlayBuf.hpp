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
typedef void (XPlayBuf::*LoopFunc)(const int &nSamples, const int &outSample,
                                   const Loop &loop, const FadeFunc writeFunc,
                                   double mix);
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

  bool wrapPos(Loop &loop) const;
  double getFadeAtBounds(const Loop &loop) const;
  void loopBody_nointerp(const int &nSamples, const int &outSample,
                         const Loop &loop, const FadeFunc writeFunc, double mix);
  void loopBody_lininterp(const int &nSamples, const int &outSample,
                          const Loop &loop, const FadeFunc writeFunc, double mix);
  void loopBody_cubicinterp(const int &nSamples, const int &outSample,
                            const Loop &loop, const FadeFunc writeFunc, double mix);

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
  LoopFunc mLoopFunc;
  SndBuf *m_buf;
};

} // namespace XPlayBuf
