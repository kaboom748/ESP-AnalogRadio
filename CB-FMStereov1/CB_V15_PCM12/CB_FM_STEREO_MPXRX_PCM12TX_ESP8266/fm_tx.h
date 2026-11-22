#pragma once

static union {
  uint8_t legacy[RAM_SIZE];
  int16_t mpxQ4[RAM_SIZE / sizeof(int16_t)];
} audioRing;
static constexpr uint32_t PcmRingSamples = RAM_SIZE / sizeof(int16_t);
static constexpr uint32_t PcmRingMask = PcmRingSamples - 1;
static constexpr unsigned PcmAreaBits = 9;
static constexpr int32_t PcmCycle = 1 << PcmAreaBits;
static constexpr int32_t MpxQ4FullScale = 2047 * 16;
static volatile uint32_t head=0,tail=0;
static volatile bool timerActive=false,timerFinished=false;
static const char* volatile timerError=nullptr;
static volatile uint32_t nextDeadline=0,cycleFraction=0;
static constexpr uint32_t SAMPLE_CYCLES=cpuHz/FM_FS;
static constexpr uint32_t SAMPLE_REMAINDER=(cpuHz)%FM_FS;
static constexpr int32_t NcoStepHz = 78125;
static constexpr unsigned MpxSlopeBits = 12;
static constexpr int32_t MpxSlopeRound = 1 << (MpxSlopeBits - 1);
static constexpr int64_t MpxSlopeDenominator = int64_t(NcoStepHz) * MpxQ4FullScale;
static constexpr int32_t mpxSlope(uint32_t period) {
  return (period * int64_t(PcmCycle) * deviationHz * (1LL << MpxSlopeBits) +
          MpxSlopeDenominator / 2) / MpxSlopeDenominator;
}
static constexpr int32_t MpxSlopeShort = mpxSlope(SAMPLE_CYCLES);
static constexpr int32_t MpxSlopeLong = mpxSlope(SAMPLE_CYCLES + 1);
static constexpr int32_t MpxNearestSwitch =
    (MpxSlopeDenominator + 2 * deviationHz - 1) / (2 * deviationHz);
static_assert(int64_t(MpxSlopeLong) * MpxQ4FullScale + MpxSlopeRound <= INT32_MAX,
              "MPX conversion must fit the native 32-bit multiply");
static constexpr uint32_t TIMER_DIV_CYCLES=2u; // APB a 80 MHz, CPU a 160 MHz, TIM_DIV1
static constexpr int32_t REARM_GUARD_CYCLES=224;
static constexpr uint32_t MAX_CATCHUP_SAMPLES=8;
static inline uint32_t occupancyBytes(){return head-tail;}
static uint32_t savedMacMask=0;
static bool macLoan=false;
static bool txPcmMode = false;
static volatile uint32_t txPcmMissed = 0;
static volatile uint32_t txPcmUnderruns = 0;
static volatile uint32_t txPcmTailUnderruns = 0;
static volatile uint32_t txPcmMinQueued = 0;

struct MpxLevel {
  uint16_t nearestK;
  int16_t pulseCycles;
};
static MpxLevel levels[256];
static uint32_t rfWord=0,lastTone=0;
static int32_t phaseDebt=0;
static int32_t previousPhaseDebt=0;
static uint32_t pulseRandom=1u;
static constexpr int32_t MIN_PULSE_CYCLES=64;
static constexpr int32_t PCM_MIN_PULSE_CYCLES=34;
static constexpr int32_t TONE_WRITE_CYCLES=32;
static constexpr int32_t ISR_EXIT_CYCLES=224;
static constexpr int32_t MAX_PULSE_CYCLES=int32_t(SAMPLE_CYCLES*51u/100u);
static_assert(PCM_MIN_PULSE_CYCLES > TONE_WRITE_CYCLES &&
              (uint64_t(PCM_MIN_PULSE_CYCLES) << (PcmAreaBits + 16)) <= UINT32_MAX,
              "PCM pulse must fit the RF timing and probability bounds");

static void prepareMpxLevels(){
  constexpr int64_t denominator=127LL*78125LL;
  for(uint32_t b=0;b<256u;++b){
    const int32_t sample=int8_t(b);
    const int64_t scaled=int64_t(sample)*deviationHz*65536LL;
    const int32_t q=carrierQ16+int32_t((scaled+(scaled>=0?denominator/2:-denominator/2))/denominator);
    const int32_t nearest=(q+32768)>>16;
    const int32_t area=(q-nearest*65536)*int32_t(SAMPLE_CYCLES);
    levels[b]={uint16_t(uint32_t(nearest)&K_MASK),int16_t((area+(area>=0?32768:-32768))/65536)};
  }
}

template<unsigned AreaBits, int32_t Minimum>
static inline __attribute__((always_inline)) bool emitMinimumPulse(uint32_t width){
  uint32_t random=pulseRandom;
  random^=random<<13;
  random^=random>>17;
  random^=random<<5;
  pulseRandom=random;
  return (random&0xffffu)*uint32_t(Minimum<<AreaBits)<(width<<16);
}

template<unsigned AreaBits>
static inline __attribute__((always_inline)) void writeMpxTone(uint32_t word) {
  if (AreaBits == PcmAreaBits) {
    // Finish each tone transition before the cycle count is sampled.
    asm volatile("s32i %0, %1, 0\n\tmemw" :: "r"(word), "r"(TONE1) : "memory");
  } else {
    wr32(TONE1, word);
    memw();
  }
}

template<unsigned AreaBits>
static inline __attribute__((always_inline)) void emitMpx(uint16_t nearestK,int32_t target,uint32_t deadline){
  constexpr int32_t minimum = AreaBits == PcmAreaBits ? PCM_MIN_PULSE_CYCLES : MIN_PULSE_CYCLES;
  const uint32_t baseline=rfWord|nearestK;
  if(lastTone!=baseline){
    writeMpxTone<AreaBits>(baseline);
    lastTone=baseline;
  }

  // Reporter l'erreur mesuree et la moitie de sa variation pour attenuer
  // le bruit basse frequence du quantificateur d'impulsions.
  const int32_t correction=phaseDebt+(phaseDebt-previousPhaseDebt)/2;
  int32_t area=target+correction;
  const bool up=area>=0;
  const int32_t magnitude=up?area:-area;
  int32_t width=magnitude>>AreaBits;
  // Tirer seulement les impulsions sous le minimum pour casser les motifs
  // coherents avec le pilote. La duree effectivement emise est deduite.
  if(magnitude>0 && width<minimum && emitMinimumPulse<AreaBits,minimum>(uint32_t(magnitude)))
    width=minimum;
  if(width>=minimum){
    if(width>MAX_PULSE_CYCLES)width=MAX_PULSE_CYCLES;
    constexpr uint32_t centerAdvance = AreaBits == PcmAreaBits ? 96u : 0u;
    const uint32_t start=deadline-(SAMPLE_CYCLES+uint32_t(width))/2u-centerAdvance;
    while(int32_t(cyc()-start)<0){}
    const int32_t room=int32_t(deadline-cyc())-ISR_EXIT_CYCLES-TONE_WRITE_CYCLES;
    if(width>room)width=room;
    if(width>=minimum){
      const uint32_t neighbor=(uint32_t(nearestK)+(up?1u:K_MASK))&K_MASK;
      writeMpxTone<AreaBits>(rfWord | neighbor);
      const uint32_t pulseStart=cyc();
      const uint32_t stop=pulseStart+uint32_t(width-TONE_WRITE_CYCLES);
      while(int32_t(cyc()-stop)<0){}
      writeMpxTone<AreaBits>(baseline);
      const int32_t actual=int32_t(cyc()-pulseStart);
      const int32_t actualArea=actual<<AreaBits;
      area+=up?-actualArea:actualArea;
    }
  }
  previousPhaseDebt=phaseDebt;
  const int32_t maxDebt=int32_t(SAMPLE_CYCLES)<<AreaBits;
  phaseDebt=constrain(area,-maxDebt,maxDebt);
}

static inline void IRAM_ATTR silent(){
  volatile uint32_t* tone=reinterpret_cast<volatile uint32_t*>(TONE1);
  *tone=(*tone)&~GATE_MASK;__asm__ volatile("memw" ::: "memory");
}
static inline __attribute__((always_inline)) void advanceMpxDeadline(
    uint32_t& deadline, uint32_t& fraction) {
  deadline += SAMPLE_CYCLES;
  fraction += SAMPLE_REMAINDER;
  if (fraction >= FM_FS) {
    ++deadline;
    fraction -= FM_FS;
  }
}
static void IRAM_ATTR tick(){
  if(!timerActive){silent();timer1_disable();return;}
  uint32_t consume=1;
  uint32_t deadline = nextDeadline;
  uint32_t fraction = cycleFraction;
  advanceMpxDeadline(deadline, fraction);
  // Un retard isole ne doit pas decaler l'horloge du pilote. Sauter au
  // bon echantillon au lieu de repartir avec une nouvelle origine de temps.
  while(int32_t(deadline-cyc())<REARM_GUARD_CYCLES){
    if(consume>=MAX_CATCHUP_SAMPLES){
      timerError="TX_TIMING";silent();timerActive=false;timerFinished=true;timer1_disable();return;
    }
    ++consume;
    advanceMpxDeadline(deadline, fraction);
  }
  nextDeadline = deadline;
  cycleFraction = fraction;
  const uint32_t t=tail;
  const uint32_t queued=head-t;
  if (txPcmMode) {
    if (consume > 1) txPcmMissed += consume - 1;
    if (queued < txPcmMinQueued) txPcmMinQueued = queued;
    if (queued < consume) {
      ++txPcmUnderruns;
      ++txPcmTailUnderruns;
    } else if (txPcmTailUnderruns != 0) {
      txPcmTailUnderruns = 0;
    }
  }
  // Un manque d'échantillons donne du silence. Le délai UART coupe la porteuse.
  if(queued>=consume){
    tail=t+consume;
  }
  // Rearmer avant l'impulsion pour que sa largeur ne change pas la cadence.
  const int32_t remain=int32_t(deadline-cyc());
  timer1_write(uint32_t(remain<32?32:remain)/TIMER_DIV_CYCLES);
  if (txPcmMode) {
    const int32_t sample = queued >= consume ? audioRing.mpxQ4[(t + consume - 1u) & PcmRingMask] : 0;
    // The fractional timer has just advanced. A carry selects the
    // 1,334-cycle conversion without rounding the multiplex down to PCM12.
    const bool longPeriod = fraction < SAMPLE_REMAINDER;
    const int32_t toneStep = int32_t(SAMPLE_CYCLES + longPeriod) * PcmCycle;
    const int32_t slope = longPeriod ? MpxSlopeLong : MpxSlopeShort;
    int32_t area = (sample * slope + MpxSlopeRound) >> MpxSlopeBits;
    uint16_t tone = baseK;
    if (sample >= MpxNearestSwitch) {
      ++tone;
      area -= toneStep;
    } else if (sample <= -MpxNearestSwitch) {
      --tone;
      area += toneStep;
    }
    emitMpx<PcmAreaBits>(tone, area, deadline);
  } else {
    const uint8_t sample = queued >= consume ? audioRing.legacy[(t + consume - 1u) & RING_MASK] : 0;
    const MpxLevel level = levels[sample];
    emitMpx<0>(level.nearestK, level.pulseCycles, deadline);
  }
}
static void startFM(){
  const uint32_t ps=xt_rsil(15);
  gateOff();toneProgramOff();
  lastTone=rd32(TONE1);
  rfWord=(lastTone&~(K_MASK|SCALE_MASK|GATE_MASK))|GATE_MASK;
  phaseDebt=previousPhaseDebt=0;
  txPcmMinQueued = occupancyBytes();
  pulseRandom=cyc()|1u;
  savedMacMask=rd32(MAC_INT_ENA);wr32(MAC_INT_ENA,0);memw();macLoan=true;
  timer1_disable();timer1_detachInterrupt();timer1_isr_init();timer1_attachInterrupt(tick);
  nextDeadline=cyc()+SAMPLE_CYCLES;cycleFraction=0;
  timerError=nullptr;timerFinished=false;timerActive=true;
  timer1_enable(TIM_DIV1,TIM_EDGE,TIM_SINGLE);
  timer1_write(SAMPLE_CYCLES/TIMER_DIV_CYCLES);
  xt_wsr_ps(ps);memw();
}
static void stopFM(){
  const uint32_t ps=xt_rsil(15);
  timerActive=false;timer1_disable();timer1_detachInterrupt();
  timerFinished=false;timerError=nullptr;
  silent();
  if(macLoan){wr32(MAC_INT_ENA,savedMacMask);memw();macLoan=false;}
  xt_wsr_ps(ps);memw();
}

