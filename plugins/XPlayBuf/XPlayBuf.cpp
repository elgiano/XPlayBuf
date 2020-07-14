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
    m_prevtrig(0),
    m_fadeSamples(1),
    m_rFadeSamples(1),
    m_remainingFadeSamples(0),
    m_playbackRate(1),
    m_numWriteChannels(0) {
    set_calc_function<XPlayBuf, &XPlayBuf::next>();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// input utils

inline bool XPlayBuf::getBuf(int nSamples) {
    float fbufnum = in0(UGenInput::bufnum);
    if (fbufnum < 0.f)
        fbufnum = 0.f;
    if (fbufnum != m_fbufnum) {
        uint32 bufnum = static_cast<uint32>(fbufnum);
        if (bufnum >= mWorld->mNumSndBufs) {
            int localBufNum = bufnum - mWorld->mNumSndBufs;
            if (localBufNum <= mParent->localBufNum) {
                m_buf = mParent->mLocalSndBufs + localBufNum;
            } else {
                bufnum = 0;
                m_buf = mWorld->mSndBufs + bufnum;
            }
        } else {
            m_buf = mWorld->mSndBufs + bufnum;
        }
        m_fbufnum = fbufnum;
    }

    LOCK_SNDBUF(m_buf);

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
                m_numWriteChannels = sc_min(mNumOutputs, m_buf->channels);
                m_failedBufNum = fbufnum;
            } else {
                m_numWriteChannels = mNumOutputs;
            }
        }
    }
    return true;
}

void XPlayBuf::updateLoop() {
    m_currLoop.start = sc_wrap(static_cast<double>(in0(UGenInput::startPos)) * m_buf->samplerate, 0.,
                               static_cast<double>(m_buf->frames));

    double loopMax = static_cast<double>(m_loop ? m_buf->frames : m_buf->frames - 1);
    m_currLoop.samples = in0(UGenInput::loopDur) * m_buf->samplerate;
    m_currLoop.end = m_currLoop.samples < 0 ? loopMax : sc_wrap(m_currLoop.start + m_currLoop.samples, 0., loopMax);
    m_currLoop.samples = m_currLoop.end - m_currLoop.start;

    m_currLoop.phase = m_currLoop.start;
}

void XPlayBuf::readInputs() {
    m_playbackRate = static_cast<double>(in0(UGenInput::playbackRate)) * m_buf->samplerate * sampleDur();
    m_loop = static_cast<bool>(in0(UGenInput::looping));
    m_fadeSamples = static_cast<double>(in0(UGenInput::fadeTime) * m_buf->samplerate);
    m_rFadeSamples = m_fadeSamples > 0 ? sc_reciprocal(m_fadeSamples) : 1;
    float trig = in0(UGenInput::trig);

    if (m_currLoop.start < 0 && m_currLoop.phase < 0)
        updateLoop();
    else if (trig > 0.f && m_prevtrig <= 0.f) {
        mDone = false;
        m_prevLoop = m_currLoop;
        updateLoop();
        m_remainingFadeSamples = m_fadeSamples;
    }
    m_prevtrig = trig;

    m_fadeFunc = in0(UGenInput::fadeEqualPower) > 0 ? &XPlayBuf::overwrite_equalPower : &XPlayBuf::overwrite_lin;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// next

void XPlayBuf::next(int nSamples) {
    if (!getBuf(nSamples))
        return;
    readInputs();
    for (int i = 0; i < nSamples; ++i) {
        mDone = wrapPos(m_currLoop);
        loopBody(i, m_currLoop, &XPlayBuf::write, getFadeAtBounds(m_currLoop));
        m_currLoop.phase += m_playbackRate;
        if (m_remainingFadeSamples > 0) {
            wrapPos(m_prevLoop);
            loopBody(i, m_prevLoop, m_fadeFunc, getFadeAtBounds(m_prevLoop) * m_remainingFadeSamples * m_rFadeSamples);
            m_remainingFadeSamples -= sc_abs(m_playbackRate);
            m_prevLoop.phase += m_playbackRate;
        }
    }
    RELEASE_SNDBUF_SHARED(m_buf);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fade functions

void XPlayBuf::write(const int& channel, const int& OUT_SAMPLE, const float& in, const double& mix) {
    out(channel)[OUT_SAMPLE] = in * mix;
}

void XPlayBuf::overwrite_equalPower(const int& channel, const int& OUT_SAMPLE, const float& in, const double& mix) {
    int32 ipos = (int32)(2048.f * mix);
    ipos = sc_clip(ipos, 0, 2048);

    float fadeinamp = ft->mSine[2048 - ipos];
    float fadeoutamp = ft->mSine[ipos];
    out(channel)[OUT_SAMPLE] = out(channel)[OUT_SAMPLE] * fadeinamp + in * fadeoutamp;
}

void XPlayBuf::overwrite_lin(const int& channel, const int& OUT_SAMPLE, const float& in, const double& mix) {
    out(channel)[OUT_SAMPLE] = out(channel)[OUT_SAMPLE] * (1 - mix) + in * (mix);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// main loop utils

inline bool XPlayBuf::wrapPos(Loop& loop) const {
    // avoid the divide if possible
    loop.phase = sc_wrap(loop.phase, 0., static_cast<double>(m_buf->frames));

    if (loop.samples < 0) {
        if (!(loop.phase > loop.end && loop.phase < loop.start) || m_playbackRate == 0)
            return false;
        if (!m_loop) {
            loop.phase = loop.end;
            return true;
        }
        if (loop.phase == loop.start || loop.phase == loop.end)
            return false;
        if (m_playbackRate > 0) {
            loop.phase -= loop.samples;
        } else {
            loop.phase += loop.samples;
        }
        if (!(loop.phase > loop.end && loop.phase < loop.start))
            return false;
    }

    if (loop.phase >= loop.end) {
        if (!m_loop) {
            loop.phase = loop.end;
            return true;
        }
        loop.phase -= loop.samples;
        if (loop.phase < loop.end)
            return false;
    } else if (loop.phase < loop.start) {
        if (!m_loop) {
            loop.phase = loop.start;
            return true;
        }
        loop.phase += loop.samples;
        if (loop.phase >= loop.start)
            return false;
    } else
        return false;

    loop.phase -= loop.samples * floor((loop.phase - loop.start) / loop.samples);
    return false;
}

double XPlayBuf::getFadeAtBounds(const Loop& loop) const {
    double mix = 1.;
    // loop start
    double distance = loop.phase - loop.start;
    if (distance >= 0 && distance < m_fadeSamples) {
        mix *= distance * m_rFadeSamples;
    };
    // loop end
    distance = loop.phase - (loop.end - m_fadeSamples);
    if (distance > 0 && distance <= m_fadeSamples) {
        mix *= 1 - distance * m_rFadeSamples;
    };
    // buf start
    if (loop.phase >= 0 && loop.phase < m_fadeSamples) {
        mix *= distance * m_rFadeSamples;
    };
    // buf end
    distance = loop.phase - (m_buf->frames - m_fadeSamples);
    if (distance > 0 && distance <= m_fadeSamples) {
        mix *= 1 - distance * m_rFadeSamples;
    };
    return mix;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// main loop

void XPlayBuf::loopBody(const int& outSample, const Loop& loop, const FadeFunc writeFunc, double mix) {
    uint32 bufChannels = m_buf->channels;
    int32 iphase = static_cast<int32>(loop.phase);
    const float* table1 = m_buf->data + iphase * bufChannels;
    const float* table0 = table1 - bufChannels;
    const float* table2 = table1 + bufChannels;
    const float* table3 = table2 + bufChannels;
    int guardFrame = m_buf->frames - 2;
    if (iphase == 0) {
        if (m_loop) {
            table0 += m_buf->samples;
        } else {
            table0 += bufChannels;
        }
    } else if (iphase >= guardFrame) {
        if (iphase == guardFrame) {
            if (m_loop) {
                table3 -= m_buf->samples;
            } else {
                table3 -= bufChannels;
            }
        } else {
            if (m_loop) {
                table2 -= m_buf->samples;
                table3 -= m_buf->samples;
            } else {
                table2 -= bufChannels;
                table3 -= 2 * bufChannels;
            }
        }
    }
    int32 index = 0;
    double fracphase = loop.phase - iphase;

    for (uint32 channel = 0; channel < m_numWriteChannels; ++channel) {
        (this->*writeFunc)(channel, outSample,
                           cubicinterp(fracphase, table0[index], table1[index], table2[index], table3[index]), mix);
        index++;
    }
    if (mNumOutputs > m_numWriteChannels) {
        for (uint32 channel = bufChannels; channel < mNumOutputs; ++channel) {
            out(channel)[outSample] = 0.f;
            index++;
        }
    }
}


} // namespace XPlayBuf

PluginLoad(XPlayBufUGens) {
    // Plugin magic
    ft = inTable;
    registerUnit<XPlayBuf::XPlayBuf>(ft, "XPlayBuf", true);
}
