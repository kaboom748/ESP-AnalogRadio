#pragma once

struct PcmMonoPair {
  int16_t right;
  int16_t left;
};

static constexpr uint32_t PcmRingSamples = RAM_SIZE / sizeof(PcmMonoPair);
static constexpr uint32_t PcmRingMask = PcmRingSamples - 1;
static_assert((PcmRingSamples & PcmRingMask) == 0, "PCM2 ring size must be a power of two");
alignas(4) static PcmMonoPair audioRing[PcmRingSamples];
static volatile uint32_t head=0,tail=0;
static volatile bool timerActive=false,timerFinished=false;
static const char* volatile timerError=nullptr;
static volatile uint32_t nextDeadline=0,cycleFraction=0;
static uint32_t savedMacMask=0;
static bool macLoan=false;
static volatile uint32_t txPcmMissed=0,txPcmUnderruns=0,txPcmTailUnderruns=0,txPcmMinQueued=0;

static constexpr uint32_t SampleCycles=cpuHz/MONO_RF_HZ;
static constexpr uint32_t SampleRemainder=cpuHz%MONO_RF_HZ;
static constexpr uint32_t TimerDivCycles=2u; // CPU 160 MHz, APB 80 MHz, TIM_DIV1.
static constexpr int32_t RearmGuardCycles=224;
static constexpr uint32_t MaxCatchupSamples=8;
static constexpr uint32_t EdgeWriteCycles=48;
static constexpr uint32_t MinimumPulseCycles=80;
static constexpr uint32_t CenterAdvanceCycles=192;
static constexpr uint32_t MaximumPulseCycles=SampleCycles/2u+1u;
static_assert(MinimumPulseCycles>EdgeWriteCycles, "Pulse must leave room for its RF edge");
static_assert(SampleCycles/2u+CenterAdvanceCycles-(MaximumPulseCycles+1u)/2u>=740u,
              "Reserve a continuous foreground window after the RF edges");

// The original HTML limits the ESP8266 PCM12 path to round(.55 * 2048).
static constexpr int32_t PcmQ4FullScale=1126*16;
static constexpr unsigned AreaBits=9;
static constexpr int32_t AreaScale=1<<AreaBits;
static constexpr unsigned SlopeBits=10;
static constexpr int32_t SlopeRound=1<<(SlopeBits-1u);
static constexpr int64_t AreaDenominator=int64_t(NcoStepHz)*PcmQ4FullScale;
static constexpr int32_t areaSlope(uint32_t period) {
  return (period*int64_t(AreaScale)*deviationHz*(1LL<<SlopeBits)+AreaDenominator/2)/AreaDenominator;
}
static constexpr int32_t ShortSlope=areaSlope(SampleCycles);
static constexpr int32_t LongSlope=areaSlope(SampleCycles+1u);
static_assert(int64_t(LongSlope)*PcmQ4FullScale+SlopeRound<=INT32_MAX,
              "PCM-to-area product must fit a signed 32-bit multiply");
static_assert((uint64_t(MinimumPulseCycles)<<(AreaBits+16u))<=UINT32_MAX,
              "Pulse probability must fit an unsigned 32-bit multiply");
static_assert(int64_t(deviationHz)*2<3LL*NcoStepHz,
              "Nearest PCM carrier code must stay within one step of centre");

static constexpr int32_t SecondToneSteps=secondCarrierOffsetHz/NcoStepHz;
static constexpr unsigned MinToneK=baseK-2u,MaxToneK=baseK+2u;
static_assert(baseK>=2u && MaxToneK+SecondToneSteps<=K_MASK,
              "Both carriers must fit their NCO fields");
static_assert(secondCarrierOffsetHz%NcoStepHz==0, "Carrier spacing must use whole NCO steps");

struct ToneWords {
  uint32_t right;
  uint32_t left;
};
static ToneWords toneWords[MaxToneK-MinToneK+1u];
static unsigned activeRight=UINT32_MAX,activeLeft=UINT32_MAX;
struct MonoState {
  int32_t debt;
  uint32_t random;
};
struct MonoPulse {
  unsigned baseline;
  unsigned neighbor;
  int32_t area;
  uint32_t width;
};
static MonoState rightState,leftState;

static inline uint32_t queuedPairs(){return head-tail;}

static void prepareToneWords() {
  constexpr uint8_t digitalBackoff=24;
  constexpr uint32_t scale=uint32_t(uint8_t(0u-digitalBackoff))<<SCALE_SHIFT;
  const uint32_t right=(rd32(TONE1)&~(K_MASK|SCALE_MASK|GATE_MASK))|GATE_MASK|scale;
  const uint32_t left=(rd32(TONE2)&0xF0000000u)|GATE_MASK|scale;
  for(unsigned k=MinToneK;k<=MaxToneK;++k){
    toneWords[k-MinToneK]={right|k,left|(k+SecondToneSteps)};
  }
}

static inline __attribute__((always_inline)) void writeMonoTones(unsigned right,unsigned left) {
  static_assert(TONE2==TONE1+4u, "Paired tone writes use adjacent registers");
  if(right!=activeRight){
    const uint32_t word=toneWords[right-MinToneK].right;
    if(left!=activeLeft){
      const uint32_t other=toneWords[left-MinToneK].left;
      asm volatile("s32i %0, %2, 0\n\ts32i %1, %2, 4\n\tmemw"
                   :: "r"(word), "r"(other), "r"(TONE1) : "memory");
      activeLeft=left;
    } else {
      asm volatile("s32i %0, %1, 0\n\tmemw" :: "r"(word), "r"(TONE1) : "memory");
    }
    activeRight=right;
  } else if(left!=activeLeft){
    const uint32_t word=toneWords[left-MinToneK].left;
    asm volatile("s32i %0, %1, 0\n\tmemw" :: "r"(word), "r"(TONE2) : "memory");
    activeLeft=left;
  }
}

static inline __attribute__((always_inline)) bool emitMinimumPulse(MonoState& state,uint32_t area) {
  uint32_t random=state.random;
  random^=random<<13;
  random^=random>>17;
  random^=random<<5;
  state.random=random;
  constexpr uint32_t minimumArea=MinimumPulseCycles*AreaScale;
  return (random&0xffffu)*minimumArea<(area<<16);
}

static inline __attribute__((always_inline)) MonoPulse makePulse(
    int16_t sample,MonoState& state,int32_t slope,int32_t stepArea) {
  int32_t area=(int32_t(sample)*slope+SlopeRound)>>SlopeBits;
  unsigned baseline=baseK;
  if(area>=stepArea/2){++baseline;area-=stepArea;}
  else if(area<=-stepArea/2){--baseline;area+=stepArea;}
  area+=state.debt;
  const uint32_t distance=area<0?uint32_t(-area):uint32_t(area);
  constexpr uint32_t minimumArea=MinimumPulseCycles*AreaScale;
  uint32_t width;
  if(distance<minimumArea)
    width=distance && emitMinimumPulse(state,distance)?MinimumPulseCycles:0;
  else
    width=(distance+AreaScale/2u)>>AreaBits;
  if(width>MaximumPulseCycles)width=MaximumPulseCycles;
  return {baseline,area<0?baseline-1u:baseline+1u,area,width};
}

static inline __attribute__((always_inline)) void settlePulse(
    MonoState& state,const MonoPulse& pulse,uint32_t elapsed,int32_t limit) {
  const int32_t correction=int32_t(elapsed*AreaScale);
  const int32_t debt=pulse.area+(pulse.area<0?correction:-correction);
  state.debt=constrain(debt,-limit,limit);
}

static inline __attribute__((always_inline)) void waitEdge(uint32_t edge) {
  while(int32_t(cyc()-edge)<0){}
}

static void IRAM_ATTR __attribute__((noinline,optimize("O2")))
emitMonoPair(PcmMonoPair pair,uint32_t period,uint32_t deadline) {
  const int32_t slope=period==SampleCycles?ShortSlope:LongSlope;
  const int32_t stepArea=int32_t(period)*AreaScale;
  const MonoPulse right=makePulse(pair.right,rightState,slope,stepArea);
  const MonoPulse left=makePulse(pair.left,leftState,slope,stepArea);
  writeMonoTones(right.baseline,left.baseline);

  // The shorter centred pulse fits inside the longer one. Merge nearly
  // coincident edges, then correct the measured area separately per NCO.
  const bool rightOuter=right.width>=left.width;
  const uint32_t outer=rightOuter?right.width:left.width;
  const uint32_t inner=rightOuter?left.width:right.width;
  uint32_t outerElapsed=0,innerElapsed=0;
  if(outer){
    const bool merge=inner && outer-inner<2u*EdgeWriteCycles;
    const uint32_t width=merge?(outer+inner+1u)/2u:outer;
    const unsigned outerRight=rightOuter||merge?right.neighbor:right.baseline;
    const unsigned outerLeft=!rightOuter||merge?left.neighbor:left.baseline;
    const uint32_t middle=deadline-period/2u-CenterAdvanceCycles;
    const uint32_t outerBegin=middle-width/2u;
    const uint32_t outerEnd=outerBegin+width;
    waitEdge(outerBegin);
    if(int32_t(outerEnd-cyc())>=int32_t(EdgeWriteCycles)){
      writeMonoTones(outerRight,outerLeft);
      const uint32_t outerStarted=cyc();
      if(inner && !merge){
        const uint32_t innerBegin=middle-inner/2u;
        const uint32_t innerEnd=innerBegin+inner;
        waitEdge(innerBegin);
        if(int32_t(innerEnd-cyc())>=int32_t(EdgeWriteCycles)){
          writeMonoTones(right.neighbor,left.neighbor);
          const uint32_t innerStarted=cyc();
          waitEdge(innerEnd);
          writeMonoTones(outerRight,outerLeft);
          innerElapsed=cyc()-innerStarted;
        }
      }
      waitEdge(outerEnd);
      writeMonoTones(right.baseline,left.baseline);
      outerElapsed=cyc()-outerStarted;
      if(merge)innerElapsed=outerElapsed;
    }
  }
  settlePulse(rightState,right,rightOuter?outerElapsed:innerElapsed,stepArea);
  settlePulse(leftState,left,rightOuter?innerElapsed:outerElapsed,stepArea);
}

static inline __attribute__((always_inline)) uint32_t advanceDeadline(uint32_t& deadline,uint32_t& fraction) {
  uint32_t period=SampleCycles;
  fraction+=SampleRemainder;
  if(fraction>=MONO_RF_HZ){
    fraction-=MONO_RF_HZ;
    ++period;
  }
  deadline+=period;
  return period;
}

static void IRAM_ATTR __attribute__((optimize("O2"))) tick() {
  if(!timerActive){gateOff();timer1_disable();return;}
  uint32_t consume=1;
  uint32_t deadline=nextDeadline;
  uint32_t fraction=cycleFraction;
  uint32_t period=advanceDeadline(deadline,fraction);
  while(int32_t(deadline-cyc())<RearmGuardCycles){
    if(consume>=MaxCatchupSamples){
      timerError="TX_TIMING";gateOff();timerActive=false;timerFinished=true;timer1_disable();return;
    }
    ++consume;
    period=advanceDeadline(deadline,fraction);
  }
  nextDeadline=deadline;
  cycleFraction=fraction;

  const uint32_t read=tail;
  const uint32_t queued=head-read;
  if(consume>1u)txPcmMissed+=consume-1u;
  if(queued<txPcmMinQueued)txPcmMinQueued=queued;
  PcmMonoPair pair={0,0};
  if(queued>=consume){
    pair=audioRing[(read+consume-1u)&PcmRingMask];
    tail=read+consume;
    if(txPcmTailUnderruns)txPcmTailUnderruns=0;
  } else {
    ++txPcmUnderruns;
    ++txPcmTailUnderruns;
  }

  // Rearm before emitting either pulse. The remaining cycles after the
  // last RF edge belong to the foreground PCM producer and UART polling.
  const int32_t remain=int32_t(deadline-cyc());
  timer1_write(uint32_t(remain<32?32:remain)/TimerDivCycles);
  emitMonoPair(pair,period,deadline);
}

static void startFM() {
  const uint32_t ps=xt_rsil(15);
  gateOff();toneProgramOff();
  prepareToneWords();
  activeRight=activeLeft=UINT32_MAX;
  txPcmMinQueued=queuedPairs();
  const uint32_t seed=cyc()|1u;
  rightState={0,seed};
  leftState={0,seed^UINT32_C(0x9e3779b8)};
  savedMacMask=rd32(MAC_INT_ENA);wr32(MAC_INT_ENA,0);memw();macLoan=true;
  timer1_disable();timer1_detachInterrupt();timer1_isr_init();timer1_attachInterrupt(tick);
  nextDeadline=cyc()+SampleCycles;cycleFraction=SampleRemainder;
  timerError=nullptr;timerFinished=false;timerActive=true;
  timer1_enable(TIM_DIV1,TIM_EDGE,TIM_SINGLE);
  timer1_write(SampleCycles/TimerDivCycles);
  xt_wsr_ps(ps);memw();
}

static void stopFM() {
  const uint32_t ps=xt_rsil(15);
  timerActive=false;timer1_disable();timer1_detachInterrupt();
  timerFinished=false;timerError=nullptr;
  gateOff();
  if(macLoan){wr32(MAC_INT_ENA,savedMacMask);memw();macLoan=false;}
  xt_wsr_ps(ps);memw();
}
