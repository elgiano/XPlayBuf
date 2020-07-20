// PluginXPlayBuf.hpp
// Gianluca Elia (elgiano@gmail.com)

#pragma once

#include "SC_PlugIn.hpp"

namespace XPlayBuf {

enum UGenInput { bufnum, playbackRate, trig, startPos, loopDur, looping, fadeTime };

struct Loop {
    double phase = -1.;
    bool isEndGTStart = false;
    int32 start = -1;
    int32 end = -1;
    float fade = 1.;
};

class XPlayBuf : public SCUnit {
public:
    XPlayBuf();

    // Destructor
    // ~XPlayBuf();

private:
    // Calc function
    void next(int nSamples);
    bool getBuf(int nSamples);
    void loadLoopArgs();
    bool readInputs();
    void writeFrame(int outSample);
    void xfadeFrame(int outSample);

    int32 updateLoopPos(Loop& loop) const;
    int32 wrapPos(int32 iphase, const Loop& loop) const;
    float getLoopBoundsFade(const int32 iphase, const Loop& loop) const;
    bool isLoopPosOutOfBounds(const Loop& loop) const;
    bool isLoopPosOutOfBounds(const Loop& loop, const int32 iphase) const;

    float xfade_equalPower(float a, float b, double fade) const;

    // Member variables
    Loop m_currLoop;
    Loop m_prevLoop;

    int32 m_guardFrame;
    uint32 m_numWriteChannels;
    float m_fadeSamples;
    float m_OneOverFadeSamples;
    float m_remainingFadeSamples;
    float m_argLoopStart;
    float m_argLoopDur;
    float m_fbufnum;
    SndBuf* m_buf;
    double m_playbackRate;
    float m_failedBufNum;
    float m_prevtrig;

    bool m_loop;
};

} // namespace XPlayBuf
