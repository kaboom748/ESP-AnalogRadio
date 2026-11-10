#pragma once

static uint8_t audioRing[RAM_SIZE];
static volatile uint32_t head=0,tail=0;
static volatile bool timerActive=false,timerFinished=false;
static const char* volatile timerError=nullptr;
static volatile uint32_t nextDeadline=0,cycleFraction=0;
static constexpr uint32_t SAMPLE_CYCLES=cpuHz/FM_FS;
static constexpr uint32_t SAMPLE_REMAINDER=(cpuHz)%FM_FS;
static constexpr uint32_t TIMER_DIV_CYCLES=2u; // APB a 80 MHz, CPU a 160 MHz, TIM_DIV1
static constexpr int32_t REARM_GUARD_CYCLES=224;
static constexpr uint32_t MAX_CATCHUP_SAMPLES=8;
static inline uint32_t occupancyBytes(){return head-tail;}
static uint32_t savedMacMask=0;
static bool macLoan=false;

struct MpxLevel {
  uint16_t nearestK;
  int16_t pulseCycles;
};
static MpxLevel levels[256];
static uint32_t rfWord=0,lastTone=0;
static int32_t phaseDebt=0;
static constexpr int32_t MIN_PULSE_CYCLES=64;
static constexpr int32_t TONE_WRITE_CYCLES=32;
static constexpr int32_t ISR_EXIT_CYCLES=224;
static constexpr int32_t MAX_PULSE_CYCLES=int32_t(SAMPLE_CYCLES*51u/100u);

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

static void IRAM_ATTR emitMpx(uint8_t sample,uint32_t deadline){
  const MpxLevel level=levels[sample];
  const uint32_t baseline=rfWord|level.nearestK;
  if(lastTone!=baseline){
    wr32(TONE1,baseline);memw();
    lastTone=baseline;
  }

  // Une impulsion centree vers le pas voisin interpole le registre RF de
  // pas 78 125 Hz. Reporter l'erreur d'aire mesuree au tick suivant.
  int32_t area=int32_t(level.pulseCycles)+phaseDebt;
  const bool up=area>=0;
  int32_t width=up?area:-area;
  if(width>=MIN_PULSE_CYCLES){
    if(width>MAX_PULSE_CYCLES)width=MAX_PULSE_CYCLES;
    const uint32_t start=deadline-(SAMPLE_CYCLES+uint32_t(width))/2u;
    while(int32_t(cyc()-start)<0){}
    const int32_t room=int32_t(deadline-cyc())-ISR_EXIT_CYCLES-TONE_WRITE_CYCLES;
    if(width>room)width=room;
    if(width>=MIN_PULSE_CYCLES){
      const uint32_t neighbor=(uint32_t(level.nearestK)+(up?1u:K_MASK))&K_MASK;
      wr32(TONE1,rfWord|neighbor);memw();
      const uint32_t pulseStart=cyc();
      const uint32_t stop=pulseStart+uint32_t(width-TONE_WRITE_CYCLES);
      while(int32_t(cyc()-stop)<0){}
      wr32(TONE1,baseline);memw();
      const int32_t actual=int32_t(cyc()-pulseStart);
      area+=up?-actual:actual;
    }
  }
  phaseDebt=constrain(area,-int32_t(SAMPLE_CYCLES),int32_t(SAMPLE_CYCLES));
}

static inline void IRAM_ATTR silent(){
  volatile uint32_t* tone=reinterpret_cast<volatile uint32_t*>(TONE1);
  *tone=(*tone)&~GATE_MASK;__asm__ volatile("memw" ::: "memory");
}
static inline void IRAM_ATTR advanceMpxDeadline(){
  nextDeadline+=SAMPLE_CYCLES;
  cycleFraction+=SAMPLE_REMAINDER;
  if(cycleFraction>=FM_FS){++nextDeadline;cycleFraction-=FM_FS;}
}
static void IRAM_ATTR tick(){
  if(!timerActive){silent();timer1_disable();return;}
  uint32_t consume=1;
  advanceMpxDeadline();
  // Un retard isole ne doit pas decaler l'horloge du pilote. Sauter au
  // bon echantillon au lieu de repartir avec une nouvelle origine de temps.
  while(int32_t(nextDeadline-cyc())<REARM_GUARD_CYCLES){
    if(consume>=MAX_CATCHUP_SAMPLES){
      timerError="TX_TIMING";silent();timerActive=false;timerFinished=true;timer1_disable();return;
    }
    ++consume;
    advanceMpxDeadline();
  }
  const uint32_t t=tail;
  const uint32_t queued=head-t;
  uint8_t sample=0;
  // Un manque d'échantillons donne du silence. Le délai UART coupe la porteuse.
  if(queued>=consume){
    sample=audioRing[(t+consume-1u)&RING_MASK];
    tail=t+consume;
  }
  // Rearmer avant l'impulsion pour que sa largeur ne change pas la cadence.
  const int32_t remain=int32_t(nextDeadline-cyc());
  timer1_write(uint32_t(remain<32?32:remain)/TIMER_DIV_CYCLES);
  emitMpx(sample,nextDeadline);
}
static void startFM(){
  const uint32_t ps=xt_rsil(15);
  gateOff();toneProgramOff();
  lastTone=rd32(TONE1);
  rfWord=(lastTone&~(K_MASK|SCALE_MASK|GATE_MASK))|GATE_MASK;
  phaseDebt=0;
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

