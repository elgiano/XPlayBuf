// PluginXPlayBuf.cpp
// Gianluca Elia (elgiano@gmail.com)

#include "SC_PlugIn.hpp"
#include "XPlayBuf.hpp"

static InterfaceTable* ft;

namespace XPlayBuf {

XPlayBuf::XPlayBuf() {
    mCalcFunc = make_calc_function<XPlayBuf, &XPlayBuf::next>();

    m_fbufnum = -1e9f;
    m_failedBufNum = -1e9f;
    m_prevtrig = 0.;
    m_remainingFadeSamples = 0.;
    mPlaybackRate = 1.;

    next(1);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fade functions

void XPlayBuf::write(int channel, int SAMPLE_INDEX, float in, double mix=1){
  out(channel)[SAMPLE_INDEX] = in * mix;
}

void XPlayBuf::overwrite_equalPower(int channel, int SAMPLE_INDEX, float in, double mix){
  int32 ipos = (int32)(2048.f * mix);
  ipos = sc_clip(ipos, 0, 2048);

  float fadeinamp = ft->mSine[2048 - ipos];
  float fadeoutamp = ft->mSine[ipos];
  out(channel)[SAMPLE_INDEX] = out(channel)[SAMPLE_INDEX] * fadeinamp + in * fadeoutamp;
}

void XPlayBuf::overwrite_lin(int channel, int SAMPLE_INDEX, float in, double mix){
  out(channel)[SAMPLE_INDEX] = out(channel)[SAMPLE_INDEX] * (1-mix) + in * (mix);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// main loop

#define LOOP_INNER_BODY_4(SAMPLE_INDEX, WRITE_FUNC, MIX)                                                               \
    float a = table0[index];                                                                                           \
    float b = table1[index];                                                                                           \
    float c = table2[index];                                                                                           \
    float d = table3[index];                                                                                           \
    (this->*WRITE_FUNC)(channel, SAMPLE_INDEX, cubicinterp(fracphase, a, b, c, d), MIX);

inline bool XPlayBuf::wrapPos(Loop& loop) {
    // avoid the divide if possible
    if (loop.phase >= loop.end) {
        if (!mLoop) {
            loop.phase = loop.end;
            return true;
        }
        loop.phase -= loop.samples;
        if (loop.phase < loop.end)
            return false;
    } else if (loop.phase < loop.start) {
        if (!mLoop) {
            loop.phase = loop.start;
            return true;
        }
        loop.phase += loop.samples;
        if (loop.phase >= loop.start)
            return false;
    } else
        return false;

    loop.phase -= loop.samples * floor( (loop.phase-loop.start) / loop.samples );
    return false;
}


void XPlayBuf::loopBody4(int nSamples, int sampleIndex, Loop loop, FadeFunc writeFunc, double mix) {
  int bufChannels = m_buf->channels;
  int guardFrame = loop.end - 2;
  double fadeIn = loop.phase - loop.start;
  if( fadeIn >= 0 && fadeIn < m_fadeSamples ){ mix *= fadeIn * m_rFadeSamples; };
  double fadeOut = loop.phase - (loop.end - m_fadeSamples);
  if( fadeOut > 0 && fadeOut <= m_fadeSamples ){ mix *= 1 - fadeOut * m_rFadeSamples; };
  int32 iphase = (int32) loop.phase;
  const float* table1 = m_buf->data + iphase * bufChannels;
  const float* table0 = table1 - bufChannels;
  const float* table2 = table1 + bufChannels;
  const float* table3 = table2 + bufChannels;
  if (iphase == 0) {
      if (mLoop) {
          table0 += static_cast<int>(loop.samples);
      } else {
          table0 += bufChannels;
      }
  } else if (iphase >= guardFrame) {
      if (iphase == guardFrame) {
          if (mLoop) {
              table3 -= static_cast<int>(loop.samples);
          } else {
              table3 -= bufChannels;
          }
      } else {
          if (mLoop) {
              table2 -= static_cast<int>(loop.samples);
              table3 -= static_cast<int>(loop.samples);
          } else {
              table2 -= bufChannels;
              table3 -= 2 * bufChannels;
          }
      }
  }
  int32 index = 0;
  float fracphase = loop.phase - (double)iphase;
  int numOuts = numOutputs();
  if (numOuts == bufChannels) {
      for (uint32 channel = 0; channel < numOuts; ++channel) {
        LOOP_INNER_BODY_4(sampleIndex, writeFunc, mix)
        index++;
      }
  } else if (numOuts < bufChannels) {
      for (uint32 channel = 0; channel < numOuts; ++channel) {
          LOOP_INNER_BODY_4(sampleIndex, writeFunc, mix)
          index++;
      }
      index += (bufChannels - numOuts);
  } else {
      for (uint32 channel = 0; channel < bufChannels; ++channel) {
          LOOP_INNER_BODY_4(sampleIndex, writeFunc, mix)
          index++;
      }
      for (uint32 channel = bufChannels; channel < numOuts; ++channel) {
          out(channel)[sampleIndex] = 0.f;
          index++;
      }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool XPlayBuf::getBuf(int nSamples){
  Unit* unit = (Unit*) this;
  float fbufnum = in0(0);
  if (fbufnum < 0.f) fbufnum = 0.f;
  if (fbufnum != m_fbufnum) {
      uint32 bufnum = (int)fbufnum;
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
      ClearUnitOutputs(unit, nSamples);
      RELEASE_SNDBUF_SHARED(m_buf);
      return false;
  } else {
      if (m_buf->channels != numOutputs()) {
          if (mWorld->mVerbosity > -1 && !mDone && (m_failedBufNum != fbufnum)) {
              Print("Buffer UGen channel mismatch: expected %i, yet buffer has %i channels\n", numOutputs(),
                    m_buf->channels);
              m_failedBufNum = fbufnum;
          }
      }
  }
  return true;
}

void XPlayBuf::updateLoop(){
  currLoop.start = sc_wrap(static_cast<double>(in0(3)) * m_buf->samplerate, 0., static_cast<double>(m_buf->frames));

  double loopMax = static_cast<double>(mLoop ? m_buf->frames : m_buf->frames - 1);
  currLoop.samples = in0(4) * m_buf->samplerate;
  currLoop.end = currLoop.samples < 0 ? loopMax : sc_min(loopMax, currLoop.start + currLoop.samples);
  currLoop.samples = currLoop.end - currLoop.start;

  currLoop.phase = currLoop.start;
}

void XPlayBuf::readInputs(){
  mPlaybackRate = static_cast<double>(in0(1)) * m_buf->samplerate * sampleDur();
  mLoop = (int32)in0(5);

  if(currLoop.start < 0 && currLoop.phase < 0) updateLoop();

  m_fadeSamples = static_cast<double>(in0(6) * m_buf->samplerate);
  m_rFadeSamples = m_fadeSamples > 0 ? sc_reciprocal(m_fadeSamples) : 1;

  float trig = in0(2);
  if (trig > 0.f && m_prevtrig <= 0.f) {
      mDone = false;
      prevLoop = currLoop;
      updateLoop();
      m_remainingFadeSamples = m_fadeSamples;
      //Print("PREV %f: %f->%f (%f)\n", prevLoop.phase, prevLoop.start, prevLoop.end, prevLoop.samples);
      //Print("CURR %f: %f->%f (%f)\n", currLoop.phase, currLoop.start, currLoop.end, currLoop.samples);
  }
  m_prevtrig = trig;

  mFadeFunc = in0(7) > 0 ? &XPlayBuf::overwrite_equalPower : &XPlayBuf::overwrite_lin;

}

void XPlayBuf::next(int nSamples) {

    if(!getBuf(nSamples)) return;
    readInputs();

    for (int i = 0; i < nSamples; ++i) {
        mDone = wrapPos(currLoop);
        loopBody4(nSamples, i, currLoop, &XPlayBuf::write, 1);
        currLoop.phase += mPlaybackRate;
        if(m_remainingFadeSamples > 0){
          wrapPos(prevLoop);
          loopBody4(nSamples, i, prevLoop, mFadeFunc, m_remainingFadeSamples * m_rFadeSamples);
          m_remainingFadeSamples -= sc_abs(mPlaybackRate);
          prevLoop.phase += mPlaybackRate;
        }
    }
    RELEASE_SNDBUF_SHARED(m_buf);
}

} // namespace XPlayBuf

PluginLoad(XPlayBufUGens) {
    // Plugin magic
    ft = inTable;
    registerUnit<XPlayBuf::XPlayBuf>(ft, "XPlayBuf", true);
}
