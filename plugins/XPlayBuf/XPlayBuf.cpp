// PluginXPlayBuf.cpp
// Gianluca Elia (elgiano@gmail.com)

#include "XPlayBuf.hpp"
#include "SC_PlugIn.hpp"

static InterfaceTable* ft;

namespace XPlayBuf {

XPlayBuf::XPlayBuf():
    m_guardFrame(0),
    m_numWriteChannels(0),
    m_fadeSamples(1),
    m_OneOverFadeSamples(1),
    m_remainingFadeSamples(0),
    m_argLoopStart(-2),
    m_argLoopDur(-2),
    m_fbufnum(-1e9f),
    m_buf(nullptr),
    m_playbackRate(1),
    m_failedBufNum(-1e9f),
    m_prevtrig(1),
    m_loop(0) {
    set_calc_function<XPlayBuf, &XPlayBuf::next>();
}

void XPlayBuf::next(int nSamples) {
    // early exit (w. cleanup) if can't read buf or mDone is true
    if (!getBuf(nSamples) || mDone) {
        ClearUnitOutputs(this, nSamples);
        RELEASE_SNDBUF_SHARED(m_buf);
        return;
    }

    bool loopChanged = readInputs();

    for (int32 outSample = 0; outSample < nSamples; ++outSample) {
        // not looping and out of bounds, or done: clear remaining out samples, release buf and exit
        if (!m_loop && isLoopPosOutOfBounds(m_currLoop)) {
            mDone = true;
            for (uint32 ch = 0; ch < mNumOutputs; ++ch)
                for (uint32 sample = nSamples - outSample; sample > 0; --sample)
                    out(ch)[sample] = 0.f;
            RELEASE_SNDBUF_SHARED(m_buf);
            return;
        }
        // if we have samples to xfade:
        if (m_remainingFadeSamples > 0) {
            // read both currLoop and prevLoop
            xfadeFrame(outSample);
            m_remainingFadeSamples -= sc_abs(m_playbackRate);
            m_currLoop.phase += m_playbackRate;
            m_prevLoop.phase += m_playbackRate;
        } else {
            // read only currLoop
            writeFrame(outSample);
            m_currLoop.phase += m_playbackRate;
        }
        // clear eventual extra out channels
        for (uint32 ch = m_buf->channels; ch < mNumOutputs; ++ch) {
            out(ch)[outSample] = 0.f;
        }
        // update currLoop bounds: if loop args changed w/o trig, update currLoop when fade is small
        if (loopChanged && m_currLoop.fade <= m_OneOverFadeSamples) {
            double phase = m_currLoop.phase;
            loadLoopArgs();
            m_currLoop.phase = phase;
            loopChanged = false;
        }
    }
    RELEASE_SNDBUF_SHARED(m_buf);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// input utils: read arguments

// do conversions (from floats to Loop) only when inputs change
void XPlayBuf::loadLoopArgs() {
    m_currLoop.phase =
        sc_mod(static_cast<double>(m_argLoopStart) * m_buf->samplerate, static_cast<double>(m_buf->frames));
    m_currLoop.start = static_cast<int32>(m_currLoop.phase);
    if (m_argLoopDur < 0) {
        m_currLoop.end = m_buf->frames;
    } else {
        m_currLoop.end =
            sc_mod(static_cast<int32>(m_currLoop.phase + static_cast<double>(m_argLoopDur) * m_buf->samplerate),
                   m_buf->frames);
    }
    m_currLoop.isEndGTStart = m_currLoop.end > m_currLoop.start;
}

bool XPlayBuf::readInputs() {
    m_playbackRate = static_cast<double>(in0(UGenInput::playbackRate)) * m_buf->samplerate * sampleDur();
    m_loop = static_cast<bool>(in0(UGenInput::looping));
    float argFadeSamples = in0(UGenInput::fadeTime) * m_buf->samplerate;
    if (argFadeSamples != m_fadeSamples) {
        m_fadeSamples = argFadeSamples;
        m_OneOverFadeSamples = m_fadeSamples > 0 ? sc_reciprocal(m_fadeSamples) : 1.;
    }

    float argLoopStart = in0(UGenInput::startPos);
    float argLoopDur = in0(UGenInput::loopDur);
    bool loopChanged = (argLoopStart != m_argLoopStart || argLoopDur != argLoopDur);
    m_argLoopStart = argLoopStart;
    m_argLoopDur = argLoopDur;

    float trig = in0(UGenInput::trig);
    bool triggered = trig > 0.f && m_prevtrig <= 0.f;

    if (triggered) {
        mDone = false;
        m_prevLoop = m_currLoop;
        loadLoopArgs();
        m_remainingFadeSamples = m_fadeSamples;
        loopChanged = false;
    } else if (m_currLoop.start < 0) { // true only at init time
        loadLoopArgs();
        loopChanged = false;
    }
    m_prevtrig = trig;

    return loopChanged;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// write funcs: wrap loop and write all channels to out

// only currLoop (no xfade)
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

// xfade currLoop with prevLoop
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
    double prev_fracphase = m_prevLoop.phase - iphase;

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
    loop.fade = getLoopBoundsFade(new_iphase, loop);
    loop.phase += new_iphase - iphase;
    return new_iphase;
}

bool XPlayBuf::isLoopPosOutOfBounds(const Loop& loop) const {
    if (loop.isEndGTStart) {
        return loop.phase < loop.start || loop.phase > loop.end;
    }
    return (loop.phase < loop.start && loop.phase > loop.end) || (loop.phase < 0 || loop.phase > m_buf->frames);
}
bool XPlayBuf::isLoopPosOutOfBounds(const Loop& loop, const int32 iphase) const {
    if (loop.isEndGTStart) {
        return iphase < loop.start || iphase > loop.end;
    }
    return (iphase < loop.start && iphase > loop.end) || (iphase < 0 || iphase > m_buf->frames);
}

int32 XPlayBuf::wrapPos(int32 iphase, const Loop& loop) const {
    if (isLoopPosOutOfBounds(loop, iphase)) {
        if (loop.isEndGTStart) {
            iphase = loop.start + sc_mod(iphase - loop.start, loop.end - loop.start);
        } else {
            if (iphase > m_buf->frames) {
                iphase = sc_wrap(iphase, loop.start, loop.end + m_buf->frames);
                if (iphase > m_buf->frames)
                    iphase -= m_buf->frames;

            } else if (iphase < 0) {
                iphase = sc_wrap(iphase, m_buf->frames - loop.start, loop.end);
                if (iphase < m_buf->frames)
                    iphase += m_buf->frames;

            } else if (m_playbackRate > 0) {
                // end < iphase < start: playing forward
                iphase = loop.start + sc_mod(iphase - loop.end, loop.end + m_buf->frames - loop.start);
                if (iphase > m_buf->frames)
                    iphase -= m_buf->frames;

            } else {
                // end < iphase < start: backwards
                iphase = loop.end - sc_mod(loop.start - iphase, loop.end + m_buf->frames - loop.start);
                if (iphase < 0)
                    iphase += m_buf->frames;
            };
        }
    }
    return iphase;
}

float XPlayBuf::getLoopBoundsFade(const int32 iphase, const Loop& loop) const {
    float fade = 1.;
    if (loop.isEndGTStart) {
        int32 startDistance = iphase - loop.start; // loop start
        if (startDistance < m_fadeSamples) {
            fade *= startDistance * m_OneOverFadeSamples;
        };
        int32 endDistance = loop.end - iphase; // loop end
        if (endDistance < m_fadeSamples) {
            fade *= endDistance * m_OneOverFadeSamples;
        };
    } else {
        if (iphase >= loop.start) {
            int32 startDistance = iphase - loop.start; // loop start
            if (startDistance < m_fadeSamples) {
                fade *= startDistance * m_OneOverFadeSamples;
            };
            int32 bufEndDistance = m_buf->frames - iphase; // buf end
            if (bufEndDistance < m_fadeSamples) {
                fade *= bufEndDistance * m_OneOverFadeSamples;
            };
        } else if (iphase <= loop.end) {
            int32 endDistance = loop.end - iphase; // loop end
            if (endDistance < m_fadeSamples) {
                fade *= endDistance * m_OneOverFadeSamples;
            };
            if (iphase < m_fadeSamples) { // buf start
                fade *= iphase * m_OneOverFadeSamples;
            };
        }
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
