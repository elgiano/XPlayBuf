// PluginXPlayBuf.cpp
// Gianluca Elia (elgiano@gmail.com)

#include "XPlayBuf.hpp"
#include "SC_PlugIn.hpp"

static InterfaceTable* ft;

namespace XPlayBuf {

XPlayBuf::XPlayBuf():
    m_loop(0),
    m_fadeFunc(nullptr),
    m_buf(nullptr),
    m_fbufnum(-1e9f),
    m_failedBufNum(-1e9f),
    m_prevtrig(1),
    m_fadeSamples(1),
    m_rFadeSamples(1),
    m_remainingFadeSamples(0),
    m_playbackRate(1),
    m_numWriteChannels(0),
    m_guardFrame(0) {
    set_calc_function<XPlayBuf, &XPlayBuf::next>();
}

void XPlayBuf::next(int nSamples) {
    if (!getBuf(nSamples))
        return;

    bool loopChanged = readInputs();

    for (int32 outSample = 0; outSample < nSamples; ++outSample) {
        if (!m_loop && isLoopPosOutOfBounds(m_currLoop)) {
            mDone = true;
            m_currLoop.phase = (m_playbackRate > 0 ? m_currLoop.end : m_currLoop.start);
        };
        if (m_remainingFadeSamples > 0) {
            xfadeFrame(outSample);
            m_remainingFadeSamples -= sc_abs(m_playbackRate);
            m_prevLoop.phase += m_playbackRate;
        } else {
            writeFrame(outSample);
        }
        // if (mNumOutputs > m_numWriteChannels) {
        for (uint32 ch = m_buf->channels; ch < mNumOutputs; ++ch) {
            out(ch)[outSample] = 0.f;
        }
        // }
        m_currLoop.phase += m_playbackRate;
        if (loopChanged && m_currLoop.fade <= m_rFadeSamples) {
            m_argLoop.phase = m_currLoop.phase;
            m_currLoop = m_argLoop;
            loopChanged = false;
        }
    }
    RELEASE_SNDBUF_SHARED(m_buf);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// input utils

bool XPlayBuf::readInputs() {
    m_playbackRate = static_cast<double>(in0(UGenInput::playbackRate)) * m_buf->samplerate * sampleDur();
    m_loop = static_cast<bool>(in0(UGenInput::looping));
    double argFadeSamples = static_cast<double>(in0(UGenInput::fadeTime) * m_buf->samplerate);
    if (argFadeSamples != m_fadeSamples) {
        m_fadeSamples = argFadeSamples;
        m_rFadeSamples = m_fadeSamples > 0 ? sc_reciprocal(m_fadeSamples) : 1;
    }

    m_argLoop.start = sc_wrap(static_cast<double>(in0(UGenInput::startPos)) * m_buf->samplerate, 0., m_bufFrames);
    double argLoopSamples = static_cast<double>(in0(UGenInput::loopDur)) * m_buf->samplerate;
    m_argLoop.end = argLoopSamples < 0 ? m_bufFrames : sc_wrap(m_argLoop.start + argLoopSamples, 0., m_bufFrames);
    m_argLoop.samples = sc_abs(m_argLoop.end - m_argLoop.start);
    bool loopChanged = (m_argLoop.start != m_currLoop.start) || (m_argLoop.end != m_currLoop.end);

    float trig = in0(UGenInput::trig);
    bool triggered = trig > 0.f && m_prevtrig <= 0.f;

    if (triggered) {
        mDone = false;
        m_prevLoop = m_currLoop;
        m_currLoop = m_argLoop;
        m_currLoop.phase = m_currLoop.start;
        m_remainingFadeSamples = m_fadeSamples;
        loopChanged = false;
    } else if (m_currLoop.phase < 0) {
        m_currLoop = m_argLoop;
        m_currLoop.phase = m_currLoop.start;
        loopChanged = false;
    }
    m_prevtrig = trig;

    m_fadeFunc = (in0(UGenInput::fadeEqualPower) > 0) ? (&XPlayBuf::xfade_equalPower) : (&XPlayBuf::xfade_lin);
    return loopChanged;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// main loop

inline void XPlayBuf::writeFrame(const int32& outSample) {
    wrapPos(m_currLoop);
    updateLoopBoundsFade(m_currLoop);
    int32 iphase = static_cast<int32>(m_currLoop.phase);
    const float* s1 = m_buf->data + iphase * m_buf->channels;
    const float* s0 = s1 - m_buf->channels;
    const float* s2 = s1 + m_buf->channels;
    const float* s3 = s2 + m_buf->channels;
    if (iphase == 0) {
        s0 += m_buf->samples;
    } else if (iphase >= m_guardFrame) {
        s3 -= m_buf->samples;
        if (iphase > m_guardFrame) {
            s2 -= m_buf->samples;
        }
    }
    double fracphase = m_currLoop.phase - iphase;
    for (uint32 ch = 0; ch < m_numWriteChannels; ++ch) {
        out(ch)[outSample] = cubicinterp(fracphase, s0[ch], s1[ch], s2[ch], s3[ch]) * m_currLoop.fade;
    }
}

inline void XPlayBuf::xfadeFrame(const int32& outSample) {
    wrapPos(m_currLoop);
    updateLoopBoundsFade(m_currLoop);
    int32 iphase = static_cast<int32>(m_currLoop.phase);
    const float* s1 = m_buf->data + iphase * m_buf->channels;
    const float* s0 = s1 - m_buf->channels;
    const float* s2 = s1 + m_buf->channels;
    const float* s3 = s2 + m_buf->channels;
    if (iphase == 0) {
        s0 += m_buf->samples;
    } else if (iphase >= m_guardFrame) {
        s3 -= m_buf->samples;
        if (iphase > m_guardFrame) {
            s2 -= m_buf->samples;
        }
    }

    wrapPos(m_prevLoop);
    updateLoopBoundsFade(m_prevLoop);
    int32 prev_iphase = static_cast<int32>(m_prevLoop.phase);
    const float* prev_s1 = m_buf->data + prev_iphase * m_buf->channels;
    const float* prev_s0 = prev_s1 - m_buf->channels;
    const float* prev_s2 = prev_s1 + m_buf->channels;
    const float* prev_s3 = prev_s2 + m_buf->channels;
    if (prev_iphase == 0) {
        prev_s0 += m_buf->samples;
    } else if (prev_iphase >= m_guardFrame) {
        prev_s3 -= m_buf->samples;
        if (prev_iphase != m_guardFrame) {
            prev_s2 -= m_buf->samples;
        }
    }

    double fracphase = m_currLoop.phase - iphase;
    double prev_fracphase = m_prevLoop.phase - prev_iphase;
    double xfade = m_remainingFadeSamples * m_rFadeSamples;
    for (uint32 ch = 0; ch < m_numWriteChannels; ++ch) {
        out(ch)[outSample] = (this->*m_fadeFunc)(
            cubicinterp(fracphase, s0[ch], s1[ch], s2[ch], s3[ch]) * m_currLoop.fade,
            cubicinterp(prev_fracphase, prev_s0[ch], prev_s1[ch], prev_s2[ch], prev_s3[ch]) * m_prevLoop.fade, xfade);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// loop utils

inline bool XPlayBuf::isLoopPosOutOfBounds(const Loop& loop) const {
    if (loop.end > loop.start) {
        return loop.phase < loop.start || loop.phase > loop.end;
    }
    return (loop.phase < loop.start && loop.phase > loop.end) || (loop.phase < 0 || loop.phase > m_bufFrames);
}

void XPlayBuf::wrapPos(Loop& loop) const {
    // if(loop.phase < 0) { Print("< 0\n"); }
    if (isLoopPosOutOfBounds(loop)) {

        if (loop.end > loop.start) {
            loop.phase = loop.start + sc_mod(loop.phase - loop.start, loop.samples);
        } else {
            //Print("bef wrap %f\n",loop.phase);
            loop.phase = sc_wrap(loop.phase, 0., m_bufFrames);
            if(loop.phase > loop.end && loop.phase < loop.start) {
                //Print("wrap %f\n",loop.phase);
                if(m_playbackRate > 0){
                    loop.phase = loop.start + sc_mod(loop.phase - loop.end, loop.samples);
                    if(loop.phase > m_bufFrames){
                        loop.phase -= m_bufFrames;
                    }
                }else{
                    //Print("bef %f\n",loop.phase);
                    loop.phase = loop.end - sc_mod(loop.start - loop.phase, loop.samples);
                    //Print("aft %f\n",loop.phase);
                    if(loop.phase < 0){
                        loop.phase += m_bufFrames;
                    }
                };

            }
        }
    }
}

void XPlayBuf::updateLoopBoundsFade(Loop& loop) const {
    double fade = 1.;
    double distance = loop.phase - loop.start; // loop start
    if (distance < m_fadeSamples && distance >= 0) {
        fade *= distance * m_rFadeSamples;
    };
    distance = loop.end - loop.phase; // loop end
    if (distance < m_fadeSamples && distance >= 0) {
        fade *= distance * m_rFadeSamples;
    };
    if (loop.samples < 0) {
        if (loop.phase < m_fadeSamples) { // buf start
            fade *= loop.phase * m_rFadeSamples;
        };
        distance = m_bufFrames - loop.phase;
        if (distance < m_fadeSamples) { // buf end
            fade *= distance * m_rFadeSamples;
        };
    }
    loop.fade = fade;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fade functions

float XPlayBuf::xfade_equalPower(const float& a, const float& b, const double& fade) const {
    int32 ipos = sc_clip(static_cast<int32>(2048.f * fade), 0, 2048);
    return a * ft->mSine[2048 - ipos] + b * ft->mSine[ipos];
}

float XPlayBuf::xfade_lin(const float& a, const float& b, const double& fade) const {
    return static_cast<float>(a * (1 - fade) + b * fade);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// buf util

inline bool XPlayBuf::getBuf(int nSamples) {
    float fbufnum = in0(UGenInput::bufnum);
    if (fbufnum < 0.f)
        fbufnum = 0.f;
    if (fbufnum == m_fbufnum) {
        ACQUIRE_SNDBUF_SHARED(m_buf);
        return true;
    } else {
        uint32 bufnum = static_cast<uint32>(fbufnum);
        if (bufnum >= mWorld->mNumSndBufs) {
            int localBufNum = bufnum - mWorld->mNumSndBufs;
            if (localBufNum <= mParent->localBufNum) {
                m_buf = mParent->mLocalSndBufs + localBufNum;
            } else {
                m_buf = mWorld->mSndBufs;
            }
        } else {
            m_buf = mWorld->mSndBufs + bufnum;
        }
        m_fbufnum = fbufnum;

        ACQUIRE_SNDBUF_SHARED(m_buf);

        if (!m_buf->data) {
            if (mWorld->mVerbosity > -1 && !mDone && (m_failedBufNum != fbufnum)) {
                Print("Buffer UGen: no buffer data\n");
                m_failedBufNum = fbufnum;
            }
            ClearUnitOutputs(this, nSamples);
            RELEASE_SNDBUF_SHARED(m_buf);
            return false;
        } else {
            if (m_buf->channels != mNumOutputs) {
                if (mWorld->mVerbosity > -1 && !mDone && (m_failedBufNum != fbufnum)) {
                    Print("Buffer UGen channel mismatch: expected %i, yet buffer has %i "
                          "channels\n",
                          mNumOutputs, m_buf->channels);
                    m_failedBufNum = fbufnum;
                }
            }
            m_numWriteChannels = sc_min(mNumOutputs, m_buf->channels);
            m_bufFrames = static_cast<double>(m_buf->frames);
            m_guardFrame = static_cast<int32>(m_buf->frames - 2);
        }

        return true;
    }
}

} // namespace XPlayBuf

PluginLoad(XPlayBufUGens) {
    // Plugin magic
    ft = inTable;
    registerUnit<XPlayBuf::XPlayBuf>(ft, "XPlayBuf", true);
}
