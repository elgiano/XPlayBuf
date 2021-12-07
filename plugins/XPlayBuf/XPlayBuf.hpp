// PluginXPlayBuf.hpp
// Gianluca Elia (elgiano@gmail.com)

#pragma once

#include "SC_PlugIn.hpp"

namespace XPlayBuf {

enum UGenInput { bufnum, playbackRate, trig, startPos, loopDur, looping, fadeTime, xFadeTime };

struct Loop {
    double phase = -1.;
    int32 start = -1;
    int32 end = -1;
    int32 fadeoutFrame = 0;
    int32 rFadeoutFrame = 0;
    float fade = 1.;
    bool isEndGTStart = false;
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
    void readInputs();
    void writeFrame(int outSample);
    void xfadeFrame(int outSample);

    int32 updateLoopPosAndFade(Loop& loop) const;
    int32 wrapPos(int32 iphase, const Loop& loop) const;
    float getLoopBoundsFade(const int32 iphase, const Loop& loop) const;
    bool isLoopPosOutOfBounds(const Loop& loop) const;
    bool isLoopPosOutOfBounds(const Loop& loop, const int32 iphase) const;
    void startXFade();
    void startXFadeIfNeeded(int32 iphase);
    float xfade_equalPower(float a, float b, double fade) const;

    // Member variables
    Loop m_currLoop;
    Loop m_prevLoop;

    int32 m_guardFrame;
    uint16 m_numWriteChannels;
    bool m_isLooping;
    bool m_loopChanged;
    int32 m_bufFrames;
    int32 m_totalFadeSamples;
    float m_oneOverFadeSamples;
    int32 m_totalXFadeSamples;
    float m_remainingXFadeSamples;
    float m_oneOverXFadeSamples;
    float m_argLoopStart;
    float m_argLoopDur;
    float m_fbufnum;
    SndBuf* m_buf;
    double m_playbackRate;
    float m_failedBufNum;
    float m_prevtrig;


};

} // namespace XPlayBuf
