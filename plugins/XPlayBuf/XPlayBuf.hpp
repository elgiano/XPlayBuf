// PluginXPlayBuf.hpp
// Gianluca Elia (elgiano@gmail.com)

#pragma once

#include "SC_PlugIn.hpp"

namespace XPlayBuf {

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

    double wrapPos(double pos);
    void loopBody4(int nSamples, int sampleIndex, double& pos, FadeFunc writeFunc, double mix);

    void write(int channel, int SAMPLE_INDEX, float in, double mix);
    void overwrite_equalPower(int channel, int SAMPLE_INDEX, float in, double mix);
    void overwrite_lin(int channel, int SAMPLE_INDEX, float in, double mix);

    // Member variables
    double m_phase;
    double mPrevPhase;
    double mPlaybackRate;
    int32 mLoop;
    float mLoopStart;
    float mLoopEnd;
    float mLoopSamples;
    float m_prevtrig;
    float m_fbufnum;
    float m_failedBufNum;
    double m_fadeSamples;
    double m_rFadeSamples;
    double m_remainingFadeSamples;
    FadeFunc mFadeFunc;
    SndBuf* m_buf;
};


} // namespace XPlayBuf
