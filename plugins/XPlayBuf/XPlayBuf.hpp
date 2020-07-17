// PluginXPlayBuf.hpp
// Gianluca Elia (elgiano@gmail.com)

#pragma once

#include "SC_PlugIn.hpp"

namespace XPlayBuf {

enum UGenInput { bufnum, playbackRate, trig, startPos, loopDur, looping, fadeTime, fadeEqualPower };

struct Loop {
    double phase = -1.;
    double start = -1.;
    double end = -1.;
    double samples = 0.;
    double fade = 1;
};

class XPlayBuf;
typedef float (XPlayBuf::*FadeFunc)(const float&, const float&, const double&) const;

class XPlayBuf : public SCUnit {
public:
    XPlayBuf();

    // Destructor
    // ~XPlayBuf();

private:
    // Calc function
    void next(int nSamples);
    void writeFrame(const int& outSample);
    void xfadeFrame(const int& outSample);
    bool getBuf(int nSamples);
    bool readInputs();
    bool updateLoop();

    bool wrapPos(Loop& loop) const;
    bool isPosInLoopBounds(const double& pos, const Loop& loop) const;
    void updateLoopBoundsFade(Loop& loop) const;

    float xfade_equalPower(const float& a, const float& b, const double& fade) const;
    float xfade_lin(const float& a, const float& b, const double& fade) const;

    // Member variables
    Loop m_currLoop;
    Loop m_prevLoop;
    Loop m_argLoop;

    double m_playbackRate;
    bool m_loop;
    float m_prevtrig;
    float m_fbufnum;
    float m_failedBufNum;
    double m_fadeSamples;
    double m_rFadeSamples;
    double m_remainingFadeSamples;
    FadeFunc m_fadeFunc;
    SndBuf* m_buf;
    uint32 m_numWriteChannels;
    int32 m_guardFrame;
    double m_bufFrames;
};

} // namespace XPlayBuf
