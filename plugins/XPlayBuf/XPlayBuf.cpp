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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// input utils

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

bool XPlayBuf::updateLoop() {
    m_argLoop.start = sc_wrap(static_cast<double>(in0(UGenInput::startPos)) * m_buf->samplerate, 0.,
                               m_bufFrames);
    double argLoopSamples = static_cast<double>(in0(UGenInput::loopDur)) * m_buf->samplerate;
    m_argLoop.end = argLoopSamples < 0 ? m_bufFrames : sc_wrap(m_argLoop.start + argLoopSamples, 0., m_bufFrames);
    m_argLoop.samples = m_argLoop.end - m_argLoop.start;

    return (m_argLoop.start != m_currLoop.start) || (m_argLoop.end != m_currLoop.end);
}

bool XPlayBuf::isPosInLoopBounds(const double& pos, const Loop& loop) const{
    if(loop.samples > 0) {
        return pos >= loop.start && pos <= loop.end;
    } else {
        return pos >= loop.end && pos <= loop.start;
    }
}

bool XPlayBuf::readInputs() {
    m_playbackRate = static_cast<double>(in0(UGenInput::playbackRate)) * m_buf->samplerate * sampleDur();
    m_loop = static_cast<bool>(in0(UGenInput::looping));
    double argFadeSamples = static_cast<double>(in0(UGenInput::fadeTime) * m_buf->samplerate);
    if(argFadeSamples != m_fadeSamples) {
        m_fadeSamples = argFadeSamples;
        m_rFadeSamples = m_fadeSamples > 0 ? sc_reciprocal(m_fadeSamples) : 1;
    }
    float trig = in0(UGenInput::trig);
    bool triggered = trig > 0.f && m_prevtrig <= 0.f;

    bool loopChanged = updateLoop();

    if (m_currLoop.phase < 0) {
        m_currLoop = m_argLoop;
        m_currLoop.phase = m_currLoop.start;
        loopChanged = false;
    } else if (
        (loopChanged && !isPosInLoopBounds(m_currLoop.phase,m_argLoop)) ||
        triggered
    ) {
        mDone = false;
        m_prevLoop = m_currLoop;
        m_currLoop = m_argLoop;
        m_currLoop.phase = m_currLoop.start;
        m_remainingFadeSamples = m_fadeSamples;
        loopChanged = false;
    }

    m_prevtrig = trig;

    m_fadeFunc = (in0(UGenInput::fadeEqualPower) > 0) ? (&XPlayBuf::xfade_equalPower) : (&XPlayBuf::xfade_lin);
    return loopChanged;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// main loop

void XPlayBuf::next(int nSamples) {
    if (!getBuf(nSamples))
        return;

    bool loopChanged = readInputs();

    uint32 bufChannels = m_buf->channels;

    for (int32 outSample = 0; outSample < nSamples; ++outSample) {
        mDone = wrapPos(m_currLoop);
        updateLoopBoundsFade(m_currLoop);
        int32 iphase = static_cast<int32>(m_currLoop.phase);
        const float* table1 = m_buf->data + iphase * bufChannels;
        const float* table0 = table1 - bufChannels;
        const float* table2 = table1 + bufChannels;
        const float* table3 = table2 + bufChannels;
        if (iphase == 0) {
            table0 += m_buf->samples;
        } else if (iphase >= m_guardFrame) {
            table3 -= m_buf->samples;
            if (iphase > m_guardFrame) {
                table2 -= m_buf->samples;
            }
        }
        double fracphase = m_currLoop.phase - iphase;

        if (m_remainingFadeSamples <= 0) {
            for (uint32 channel = 0; channel < m_numWriteChannels; ++channel) {
                out(channel)[outSample] =
                    cubicinterp(fracphase, table0[channel], table1[channel], table2[channel], table3[channel])
                    * m_currLoop.fade;
            }
        } else {
            wrapPos(m_prevLoop);
            updateLoopBoundsFade(m_prevLoop);
            int32 prev_iphase = static_cast<int32>(m_prevLoop.phase);
            const float* prev_table1 = m_buf->data + prev_iphase * bufChannels;
            const float* prev_table0 = prev_table1 - bufChannels;
            const float* prev_table2 = prev_table1 + bufChannels;
            const float* prev_table3 = prev_table2 + bufChannels;
            if (prev_iphase == 0) {
                prev_table0 += m_buf->samples;
            } else if (prev_iphase >= m_guardFrame) {
                prev_table3 -= m_buf->samples;
                if (prev_iphase != m_guardFrame) {
                    prev_table2 -= m_buf->samples;
                }
            }
            double prev_fracphase = m_prevLoop.phase - prev_iphase;

            for (uint32 channel = 0; channel < m_numWriteChannels; ++channel) {
                out(channel)[outSample] = (this->*m_fadeFunc)(
                    cubicinterp(fracphase, table0[channel], table1[channel], table2[channel], table3[channel])
                        * m_currLoop.fade,
                    cubicinterp(prev_fracphase, prev_table0[channel], prev_table1[channel], prev_table2[channel],
                                prev_table3[channel]) * m_prevLoop.fade,
                    m_remainingFadeSamples * m_rFadeSamples
                );
            }
            m_remainingFadeSamples -= sc_abs(m_playbackRate);
            m_prevLoop.phase += m_playbackRate;
        }
        if (mNumOutputs > m_numWriteChannels) {
            for (uint32 channel = bufChannels; channel < mNumOutputs; ++channel) {
                out(channel)[outSample] = 0.f;
            }
        }
        if(m_currLoop.fade < m_rFadeSamples && loopChanged){
            m_argLoop.phase = m_currLoop.phase;
            m_currLoop = m_argLoop;
        }
        m_currLoop.phase += m_playbackRate;
    }
    RELEASE_SNDBUF_SHARED(m_buf);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// main loop utils

bool XPlayBuf::wrapPos(Loop& loop) const {
    // wrap buf extremes
    if (loop.samples < 0) {
        loop.phase = sc_wrap(loop.phase, 0., m_bufFrames);
        // in bounds: noop
        if (!(loop.phase > loop.end && loop.phase < loop.start) || m_playbackRate == 0) {
            return false;
        } else if (!m_loop) {
            loop.phase = loop.end;
            return true;
        } else {
            if(m_playbackRate > 0) {
                loop.phase = sc_wrap(loop.start + sc_mod(loop.phase - loop.end, loop.samples * -1.), 0., m_bufFrames);
            }else{
                loop.phase = sc_wrap(loop.start + sc_mod(loop.start - loop.phase, loop.samples * -1.), 0., m_bufFrames);
            }
            return false;
        }
    } else {
        if ((loop.phase >= loop.start && loop.phase < loop.end) || m_playbackRate == 0) {
            return false;
        }else if (!m_loop) {
            loop.phase = (m_playbackRate > 0 ? loop.end : loop.start);
            return true;
        }
        if (loop.phase >= loop.end) {
            loop.phase -= loop.samples;
            if (loop.phase < loop.end)
                return false;
        } else if (loop.phase < loop.start) {
            loop.phase += loop.samples;
            if (loop.phase >= loop.start)
                return false;
        }
        loop.phase -= loop.samples * floor((loop.phase - loop.start) / loop.samples);
        return false;
    }
}

void XPlayBuf::updateLoopBoundsFade(Loop& loop) const {
    loop.fade = 1.;
    double distance = loop.phase - loop.start; // loop start
    if (distance < m_fadeSamples && distance >= 0) {
        loop.fade *= distance * m_rFadeSamples;
    };
    distance = loop.end - loop.phase; // loop end
    if (distance < m_fadeSamples && distance >= 0) {
        loop.fade *= distance * m_rFadeSamples;
    };
    if(loop.samples < 0 ) {
        if (loop.phase < m_fadeSamples) { // buf start
            loop.fade *= loop.phase * m_rFadeSamples;
        };
        distance = m_bufFrames - loop.phase;
        if (distance < m_fadeSamples) { // buf end
            loop.fade *= distance * m_rFadeSamples;
        };
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fade functions

float XPlayBuf::xfade_equalPower(const float& a, const float& b, const double& fade) const {
    int32 ipos = sc_clip(static_cast<int32>(2048.f * fade), 0, 2048);
    return a * ft->mSine[2048 - ipos] + b * ft->mSine[ipos];
}

float XPlayBuf::xfade_lin(const float& a, const float& b, const double& fade) const { return static_cast<float>(a * (1 - fade) + b * fade); }

} // namespace XPlayBuf

PluginLoad(XPlayBufUGens) {
    // Plugin magic
    ft = inTable;
    registerUnit<XPlayBuf::XPlayBuf>(ft, "XPlayBuf", true);
}
