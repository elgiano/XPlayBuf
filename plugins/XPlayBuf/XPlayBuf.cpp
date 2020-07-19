// PluginXPlayBuf.cpp
// Gianluca Elia (elgiano@gmail.com)

#include "XPlayBuf.hpp"
#include "SC_PlugIn.hpp"

static InterfaceTable* ft;

namespace XPlayBuf {

XPlayBuf::XPlayBuf():
    m_loop(0),
    m_buf(nullptr),
    m_fbufnum(-1e9f),
    m_failedBufNum(-1e9f),
    m_prevtrig(1),
    m_fadeSamples(1),
    m_OneOverFadeSamples(1),
    m_remainingFadeSamples(0),
    m_playbackRate(1),
    m_numWriteChannels(0),
    m_guardFrame(0) {
    set_calc_function<XPlayBuf, &XPlayBuf::next>();
}

void XPlayBuf::next(int nSamples) {
    if (!getBuf(nSamples)) {
        ClearUnitOutputs(this, nSamples);
        RELEASE_SNDBUF_SHARED(m_buf);
        return;
    }

    bool loopChanged = readInputs();

    for (int32 outSample = 0; outSample < nSamples; ++outSample) {
        // not looping and out of bounds, or done: clear remaining out samples, release buf and exit
        if (!m_loop && (isLoopPosOutOfBounds(m_currLoop) || mDone)) {
            mDone = true;
            m_currLoop.phase = (m_playbackRate > 0 ? m_currLoop.end : m_currLoop.start);
            for (uint32 ch = 0; ch < mNumOutputs; ++ch) {
                for (uint32 sample = nSamples - outSample; sample > 0; --sample)
                    out(ch)[sample] = 0.f;
            }
            RELEASE_SNDBUF_SHARED(m_buf);
            return;
        }
        // if we have samples to xfade:
        if (m_remainingFadeSamples > 0) {
            xfadeFrame(outSample);
            m_remainingFadeSamples -= sc_abs(m_playbackRate);
            m_prevLoop.phase += m_playbackRate;
            m_currLoop.phase += m_playbackRate;
        } else {
            writeFrame(outSample);
            m_currLoop.phase += m_playbackRate;
        }
        // clear eventual extra out channels
        for (uint32 ch = m_buf->channels; ch < mNumOutputs; ++ch) {
            out(ch)[outSample] = 0.f;
        }
        if (loopChanged && m_currLoop.fade <= m_OneOverFadeSamples) {
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
    float argFadeSamples = in0(UGenInput::fadeTime) * m_buf->samplerate;
    if (argFadeSamples != m_fadeSamples) {
        m_fadeSamples = argFadeSamples;
        m_OneOverFadeSamples = m_fadeSamples > 0 ? sc_reciprocal(static_cast<double>(m_fadeSamples)) : 1;
    }

    m_argLoop.phase = sc_wrap(static_cast<double>(in0(UGenInput::startPos)) * m_buf->samplerate, 0., m_bufFrames);
    m_argLoop.start = static_cast<int32>(m_argLoop.phase);
    double argLoopSamples = static_cast<double>(in0(UGenInput::loopDur)) * m_buf->samplerate;
    m_argLoop.end = argLoopSamples < 0 ? m_buf->frames : static_cast<int32>(sc_wrap(m_argLoop.phase + argLoopSamples, 0., m_bufFrames));
    bool loopChanged = (m_argLoop.start != m_currLoop.start) || (m_argLoop.end != m_currLoop.end);

    float trig = in0(UGenInput::trig);
    bool triggered = trig > 0.f && m_prevtrig <= 0.f;

    if (triggered) {
        mDone = false;
        m_prevLoop = m_currLoop;
        m_currLoop = m_argLoop;
        m_remainingFadeSamples = m_fadeSamples;
        loopChanged = false;
    } else if (m_currLoop.phase < 0 && m_currLoop.start < 0) {
        m_currLoop = m_argLoop;
        loopChanged = false;
    }
    m_prevtrig = trig;

    return loopChanged;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// main loop

void XPlayBuf::writeFrame(int32 outSample) {
    int32 iphase = updateLoopPos(m_currLoop);
    double fracphase = m_currLoop.phase - iphase;

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
    for (uint32 ch = 0; ch < m_numWriteChannels; ++ch) {
        out(ch)[outSample] = cubicinterp(fracphase, s0[ch], s1[ch], s2[ch], s3[ch]) * m_currLoop.fade;
    }
}

void XPlayBuf::xfadeFrame(int32 outSample) {
    int32 iphase = updateLoopPos(m_currLoop);
    double fracphase = m_currLoop.phase - iphase;

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


    int32 prev_iphase = updateLoopPos(m_prevLoop);
    double prev_fracphase = m_prevLoop.phase - prev_iphase;

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

    float xfade = m_remainingFadeSamples * m_OneOverFadeSamples;
    for (uint32 ch = 0; ch < m_numWriteChannels; ++ch) {
        out(ch)[outSample] = xfade_equalPower(
            cubicinterp(fracphase, s0[ch], s1[ch], s2[ch], s3[ch]) * m_currLoop.fade,
            cubicinterp(prev_fracphase, prev_s0[ch], prev_s1[ch], prev_s2[ch], prev_s3[ch]) * m_prevLoop.fade, xfade);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// loop utils

int32 XPlayBuf::updateLoopPos(Loop& loop) const {
    int32 iphase = static_cast<int32>(loop.phase);
    int32 new_iphase = wrapPos(iphase, loop);
    loop.phase += new_iphase - iphase;
    loop.fade = getLoopBoundsFade(new_iphase, loop);
    return new_iphase;
}

bool XPlayBuf::isLoopPosOutOfBounds(const Loop& loop) const {
    if (loop.end > loop.start) {
        return loop.phase < loop.start || loop.phase > loop.end;
    }
    return (loop.phase < loop.start && loop.phase > loop.end) || (loop.phase < 0 || loop.phase > m_bufFrames);
}
// make const
int32 XPlayBuf::wrapPos(int32 iphase, const Loop& loop) const {
    if (isLoopPosOutOfBounds(loop)) {

        if (loop.end > loop.start) {
            iphase = loop.start + sc_mod(iphase - loop.start, loop.end - loop.start);
        } else {
            iphase = sc_wrap(iphase, 0, m_buf->frames);
            if(iphase > loop.end && iphase < loop.start) {
                if(m_playbackRate > 0){
                    iphase = loop.start + sc_mod(iphase - loop.end, loop.end + m_buf->frames - loop.start);
                    if(iphase > m_bufFrames){
                        iphase -= m_bufFrames;
                    }
                }else{
                    iphase = loop.end - sc_mod(loop.start - iphase, loop.end + m_buf->frames - loop.start);
                    if(iphase < 0){
                        iphase += m_bufFrames;
                    }
                };

            }
        }
    }
    return iphase;
}
// make const
float XPlayBuf::getLoopBoundsFade(int32 iphase, const Loop& loop) const {
    float fade = 1.;
    if (loop.start > loop.end) {
        if(iphase >= loop.start) {
            int32 startDistance = iphase - loop.start; // loop start
            if (startDistance < m_fadeSamples) {
                fade *= startDistance * m_OneOverFadeSamples;
            };
            int32 bufEndDistance = m_bufFrames - iphase;
            if (bufEndDistance < m_fadeSamples) { // buf end
                fade *= bufEndDistance * m_OneOverFadeSamples;
            };
        } else if( iphase <= loop.end) {
            int32 endDistance = loop.end - iphase; // loop end
            if (endDistance < m_fadeSamples) {
                fade *= endDistance * m_OneOverFadeSamples;
            };
            if (iphase < m_fadeSamples) { // buf start
                fade *= iphase * m_OneOverFadeSamples;
            };
        }

    } else {
        int32 startDistance = iphase - loop.start; // loop start
        if (startDistance < m_fadeSamples) {
            fade *= startDistance * m_OneOverFadeSamples;
        };
        int32 endDistance = loop.end - iphase; // loop end
        if (endDistance < m_fadeSamples) {
            fade *= endDistance * m_OneOverFadeSamples;
        };
    }
    return fade;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fade functions

float XPlayBuf::xfade_equalPower(float a, float b, double fade) const {
    int32 ipos = sc_clip(static_cast<int32>(2048.f * fade), 0, 2048);
    return a * ft->mSine[2048 - ipos] + b * ft->mSine[ipos];
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// buf util

bool XPlayBuf::getBuf(int nSamples) {
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
