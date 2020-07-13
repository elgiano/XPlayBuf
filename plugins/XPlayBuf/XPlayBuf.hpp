// PluginXPlayBuf.hpp
// Gianluca Elia (elgiano@gmail.com)

#pragma once

#include "SC_PlugIn.hpp"

namespace XPlayBuf {

enum UGenInput { bufnum, playbackRate, trig, startPos, loopDur, looping, fadeTime, fadeEqualPower, interpolation };

struct Loop {
    double phase = -1.;
    double start = -1.;
    double end = -1.;
    double samples = 0.;
};

class XPlayBuf;
typedef void (XPlayBuf::*FadeFunc)(const int&, const int&, const float&, const double&);

class XPlayBuf : public SCUnit {
public:
    XPlayBuf();

    // Destructor
    // ~XPlayBuf();

private:
    // Calc function
    void next_nointerp(int nSamples);
    void next_lininterp(int nSamples);
    void next_cubicinterp(int nSamples);
    bool getBuf(int nSamples);
    void readInputs();
    void updateLoop();

    bool wrapPos(Loop& loop) const;
    double getFadeAtBounds(const Loop& loop) const;
    void loopBody_nointerp(const int& outSample, const Loop& loop, const FadeFunc writeFunc, double mix);
    void loopBody_lininterp(const int& outSample, const Loop& loop, const FadeFunc writeFunc, double mix);
    void loopBody_cubicinterp(const int& outSample, const Loop& loop, const FadeFunc writeFunc, double mix);

    void write(const int& channel, const int& OUT_SAMPLE, const float& in, const double& mix);
    void overwrite_equalPower(const int& channel, const int& OUT_SAMPLE, const float& in, const double& mix);
    void overwrite_lin(const int& channel, const int& OUT_SAMPLE, const float& in, const double& mix);

    // Member variables
    Loop m_currLoop;
    Loop m_prevLoop;

    double m_playbackRate;
    int32 m_loop;
    float m_prevtrig;
    float m_fbufnum;
    float m_failedBufNum;
    double m_fadeSamples;
    double m_rFadeSamples;
    double m_remainingFadeSamples;
    FadeFunc m_fadeFunc;
    SndBuf* m_buf;
};

} // namespace XPlayBuf
