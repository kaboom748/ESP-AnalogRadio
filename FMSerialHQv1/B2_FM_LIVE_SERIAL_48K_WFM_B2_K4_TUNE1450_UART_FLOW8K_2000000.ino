/*
  B2_FM_LIVE_SERIAL_48K_WFM_B2 — FLOW8K / UART continu avec PAUSE-REPRISE
  Base : B2_WFM_MONO_HIFI_48K_CONTROLE_MODULATION_APERCU.ino

  WIFI: AUCUNE ASSOCIATION, aucune commande web ou ecriture LittleFS.
  RF: FM mono 48 kHz a K+4 (+312,5 kHz du PLL), prechauffage CW a K+8.
  UART0: 2000000 8N1, IRQ UART RX en COMMANDES, IRQ UART RX OFF en STREAM.
  AUDIO: valeurs WFM2 a 48 kHz, s16 little-endian en unites de 4 Hz,
         sans en-tete ni bourrage. File Q16 equivalente a 8192 octets UART.
         Timer1 consomme une valeur de deviation par tick (96 000 octets/s).
  TRANSPORT FLOW8K: l ESP vide la FIFO UART EN CONTINU par polling,
  aucune interruption UART RX en STREAM. Le PC envoie des blocs d'au plus 512 octets.
  Aucun credit ni ACK par tranche. 0..100 = RAM % (telemetrie).
  235 = PAUSE prioritaire a 7168 o ; 236 = REPRISE sous 5120 o.
  La reserve de 1024 o du seuil haut amortit les octets deja en route.
  La protection FIFO materielle reste necessaire meme a 2 Mbauds.
  Les codes 240..255 sont reserves aux etats/erreurs; aucun log texte sur UART.

  IMPORTANT: bas niveau PHY ESP8266 experimental; pas une radio homologuee.
  PREMIER ESSAI sur charge/liaison RF blindee ou attenuee et dans cadre legal.
  Aucun flash/firmware n'est un test de RF mesuree. Build ESP8266 160 MHz.

  Licence de la source RF SP8ESA reprise du projet B2 d'origine:
  Copyright (c) 2026 SP8ESA
  Permission is hereby granted, free of charge, to any person obtaining a
  copy of this software and associated documentation files (the "Software"),
  to deal in the Software without restriction, including without limitation
  the rights to use, copy, modify, merge, publish, distribute, sublicense,
  and/or sell copies of the Software, and to permit persons to whom the
  Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
  DEALINGS IN THE SOFTWARE.
*/
#include <Arduino.h>
#include <ESP8266WiFi.h>
extern "C" {
#include "user_interface.h"
}
#include <string.h>

// Ne contient aucun identifiant ou secret Wi-Fi.
static constexpr uint32_t RF_KHZ=2442000UL; // canal 7, experimentation
static constexpr uint8_t RF_CHANNEL=7;
static constexpr uint32_t CPU_MHZ=160UL; // Selectionner CPU Frequency 160 MHz dans Arduino
static constexpr uint32_t BAUD=2000000UL;
static constexpr uint32_t FM_FS=48000UL;
static constexpr uint32_t RAM_SIZE=8192UL;
static constexpr uint32_t BYTES_PER_SAMPLE=2u;
static constexpr uint32_t RING_SAMPLES=RAM_SIZE/BYTES_PER_SAMPLE;
static constexpr uint32_t RING_MASK=RING_SAMPLES-1u;
static constexpr uint32_t START_LEVEL=6144UL; // 64 ms avant modulation
static constexpr uint32_t HIGH_WATER=7168UL; // 87.5 %, 1024 octets de reserve
static constexpr uint32_t LOW_WATER=5120UL;  // 62.5 %, reprise hysteresis
static_assert((RING_SAMPLES & RING_MASK)==0u,"RING_SAMPLES doit etre une puissance de deux");
static_assert(LOW_WATER < START_LEVEL && START_LEVEL < HIGH_WATER,"Seuils incompatibles");
static_assert(HIGH_WATER+512u<RAM_SIZE,"Reserve FIFO/RAM insuffisante");
static constexpr uint32_t UART_TIMEOUT_MS=1000UL;
static constexpr uint32_t FLOW_REPEAT_MS=12UL; // 53 ms de reserve au seuil bas
static constexpr uint8_t ASK_CW=0;
// 4 pas K de 78 125 Hz placent le CW a 312,5 kHz au-dessus du centre FM.
static constexpr int32_t FM_K_OFFSET_STEPS=4; // Un pas K vaut 78,125 kHz.
static constexpr uint16_t CW_K_OFFSET_STEPS=4u; // Ecart CW par rapport a la FM.
static constexpr uint32_t CW_PREHEAT_MS=1000UL;
static constexpr uint8_t APWR_DEFAULT=127;

// ESP -> PC: 0..100 % RAM INFORMATIF (aucun credit !)
// 202..234: pic FIFO quantifie par 4 octets (diagnostic uniquement)
// 235: PAUSE OBLIGATOIRE, 236: REPRISE, 239: STREAM PRET
// 240..248: etats et erreurs preexistants.
static constexpr uint8_t ST_PAUSE=235;
static constexpr uint8_t ST_RESUME=236;
static constexpr uint8_t ST_STREAM_READY=239;
static constexpr uint8_t ST_READY=240;      // prechauffage, commandes disponibles
static constexpr uint8_t ST_PLAY=241;       // FM active
static constexpr uint8_t ST_FINISH=242;     // dernier echantillon lu, retour CW
static constexpr uint8_t ST_TIMEOUT=243;    // Source serie absente trop longtemps
static constexpr uint8_t ST_UNDERRUN=244;   // donnees RAM epuisees
static constexpr uint8_t ST_OVERFLOW=245;   // vraie erreur FIFO materielle
static constexpr uint8_t ST_RF_FAULT=246;   // echec RF ou cadence critique
static constexpr uint8_t ST_CMD_ERROR=247;  // commande incorrecte
static constexpr uint8_t ST_STOP_RF=248;    // RF arretee sur ordre, securite
// PC -> ESP uniquement en mode COMMANDES (ISR UART RX active) :
static constexpr uint8_t CMD_PING=0xA0;
static constexpr uint8_t CMD_LEGACY_START=0xA1;
static constexpr uint8_t CMD_START=0xA4;   // [CMD, total48kSamples u32 LE, deviationIndex, APWR]
static constexpr uint8_t CMD_RF_OFF=0xA2;
static constexpr uint8_t CMD_RF_ON=0xA3;

// UART0 ESP8266: pilote Arduino en commandes; FIFO materielle directe en STREAM.
static constexpr uint32_t UART_FIFO=0x60000000u;
static constexpr uint32_t UART_INT_RAW=0x60000004u;
static constexpr uint32_t UART_INT_ENA=0x6000000Cu;
static constexpr uint32_t UART_INT_CLR=0x60000010u;
static constexpr uint32_t UART_STATUS=0x6000001Cu;
static constexpr uint32_t UART_CONF0=0x60000020u;
static constexpr uint32_t UART_RX_OVERFLOW_RAW=(1u<<4);
static constexpr uint32_t UART_RX_FRAME_RAW=(1u<<3);
static constexpr uint32_t UART_RX_PARITY_RAW=(1u<<2);

// Registres RF d'origine : moteur WFM mono 48 kHz, sans Wi-Fi associe.
static constexpr uint32_t TONE1=0x600005B8u;
static constexpr uint32_t TONE2=0x600005BCu;
static constexpr uint32_t TONE3=0x600005C4u;
static constexpr uint32_t PBUS_CMD=0x60000594u;
static constexpr uint32_t PBUS_STAT=0x600005A0u;
static constexpr uint32_t RX_CTRL=0x60009B08u;
static constexpr uint32_t K_MASK=0x000003FFu;
static constexpr uint32_t SCALE_MASK=0x0003FC00u;
static constexpr uint32_t GATE_MASK=0x00040000u;
static constexpr uint32_t SCALE_SHIFT=10u;
static constexpr uint32_t RX_STOP=0x08000000u;
static constexpr uint32_t MAC_INT_ENA=0x3FF20C18u;
using SetTxClkFn=void(*)(int);
using SetAnaScaleFn=uint8_t(*)(uint8_t);
static SetTxClkFn set_txclk=reinterpret_cast<SetTxClkFn>(0x4000650Cu);
static SetAnaScaleFn set_ana_scale=reinterpret_cast<SetAnaScaleFn>(0x4000678Cu);

static inline void memw(){__asm__ volatile("memw" ::: "memory");}
static inline uint32_t rd32(uint32_t addr){return *reinterpret_cast<volatile uint32_t*>(addr);}
static inline void wr32(uint32_t addr,uint32_t val){*reinterpret_cast<volatile uint32_t*>(addr)=val;}
static inline uint32_t IRAM_ATTR cyc(){uint32_t c;__asm__ volatile("rsr %0,ccount":"=a"(c));return c;}
static inline uint8_t askCode(uint8_t ask){return uint8_t(0u-ask);}
static inline uint8_t fifoCount(){return uint8_t(rd32(UART_STATUS)&0xffu);}
static inline uint8_t txCount(){return uint8_t((rd32(UART_STATUS)>>16)&0xffu);}
static inline bool uartSend(uint8_t b){
  if(txCount()>=127u)return false; // jamais attendre la TX ni le PC
  wr32(UART_FIFO,b);return true;
}
static inline void uartFlushRx(){
  uint32_t conf=rd32(UART_CONF0);wr32(UART_CONF0,conf|(1u<<17));
  wr32(UART_CONF0,conf&~(1u<<17));
  wr32(UART_INT_CLR,0x1ffu);memw();
}
// Le core Arduino ESP8266 installe une ISR et un buffer RX avec Serial.begin().
// On conserve cette ISR UNIQUEMENT pendant les commandes; en STREAM on lit
// directement les octets disponibles dans la FIFO. Aucun Serial.* en STREAM.
static uint32_t commandIrqMask=0u;
static bool uartStreaming=false;
static void uartInit(){
  Serial.begin(BAUD,SERIAL_8N1);
  Serial.setDebugOutput(false); // Ne jamais melanger logs texte et protocole binaire.
  Serial.flush();
  commandIrqMask=rd32(UART_INT_ENA); // Masque installe par le core Arduino.
  // Ne PAS masquer les IRQ ici: PING/START/STATUS utilisent Serial.read().
}
static void uartEnterStream(){
  // START consomme avec Serial.read(); le PC attendra ST_STREAM_READY
  // avant de transmettre la moindre donnee audio.
  while(Serial.available()>0) (void)Serial.read();
  const uint32_t ps=xt_rsil(15);
  commandIrqMask=rd32(UART_INT_ENA);
  wr32(UART_INT_ENA,0u);  // Desactive UNIQUEMENT les sources UART.
  uartFlushRx();          // FIFO reinitialisee, sans toucher aux RX de Timer1.
  uartStreaming=true;
  xt_wsr_ps(ps);memw();
}
static void uartReturnCommands(){
  if(!uartStreaming)return;
  const uint32_t ps=xt_rsil(15);
  wr32(UART_INT_ENA,0u);
  uartFlushRx();          // Elimine les octets audio residuels.
  // Handler Arduino toujours installe: retablir ses IRQ RX sans Serial.begin().
  wr32(UART_INT_ENA,commandIrqMask);
  uartStreaming=false;
  xt_wsr_ps(ps);memw();
}
static inline int32_t khzToQ16(int32_t offKHz){
  return int32_t((int64_t)offKHz*64LL*65536LL/5000LL);
}
static uint16_t baseK=0;
static int32_t carrierQ16=0;
static bool rfReady=false,rfEnabled=false;
static uint8_t selectedApwr=APWR_DEFAULT;
struct RfRegisters {
  uint32_t tone1,tone2,tone3,pbusCmd,rxCtrl;
  uint16_t pbus2,pbus7,pbus1,pbus6;
};
static RfRegisters savedRf;
static bool rfOwned=false;
static inline void gateOff(){wr32(TONE1,rd32(TONE1)&~GATE_MASK);memw();}
static bool pbusWrite(uint8_t sel,uint8_t bank,uint16_t value){
  uint32_t cmd=rd32(PBUS_CMD);
  cmd=(cmd&0xFFFF0001u)|((uint32_t(bank)&3u)<<14)|((uint32_t(value)&0x1ffu)<<5)|((uint32_t(sel)&7u)<<2)|2u;
  wr32(PBUS_CMD,cmd);memw();
  const uint32_t start=micros();
  bool ok=true;
  while(rd32(PBUS_STAT)&0x80000000u){
    if(uint32_t(micros()-start)>2500u){ok=false;break;}
    delay(0);
  }
  wr32(PBUS_CMD,rd32(PBUS_CMD)&~2u);memw();return ok;
}
static void enterManual(){wr32(RX_CTRL,rd32(RX_CTRL)|RX_STOP);memw();wr32(PBUS_CMD,rd32(PBUS_CMD)|1u);memw();}
static bool txPathOn(){return pbusWrite(2,1,1)&&pbusWrite(7,1,95)&&pbusWrite(1,1,127)&&pbusWrite(6,1,127);}
static uint16_t pbusRead(uint8_t sel,uint8_t bank){
  using ReadFn=uint32_t(*)(uint32_t,uint32_t);
  return uint16_t(reinterpret_cast<ReadFn>(0x400074D8u)(sel,bank)&0x1ffu);
}
static void saveRfState(){
  savedRf={rd32(TONE1),rd32(TONE2),rd32(TONE3),rd32(PBUS_CMD),rd32(RX_CTRL),
           pbusRead(2,1),pbusRead(7,1),pbusRead(1,1),pbusRead(6,1)};
  rfOwned=true;
}
static bool releaseOfflineRf(){
  bool ok=true;
  if(rfOwned){
    gateOff();set_txclk(0);
    wr32(PBUS_CMD,rd32(PBUS_CMD)|1u);memw();
    ok=pbusWrite(6,1,savedRf.pbus6)&&ok;
    ok=pbusWrite(1,1,savedRf.pbus1)&&ok;
    ok=pbusWrite(7,1,savedRf.pbus7)&&ok;
    ok=pbusWrite(2,1,savedRf.pbus2)&&ok;
    wr32(TONE1,savedRf.tone1&~GATE_MASK);
    wr32(TONE2,savedRf.tone2&~GATE_MASK);
    wr32(TONE3,savedRf.tone3&~GATE_MASK);memw();
    wr32(RX_CTRL,savedRf.rxCtrl);
    wr32(PBUS_CMD,savedRf.pbusCmd&~2u);memw();
    rfOwned=false;
  }
  rfEnabled=false;
  return WiFi.mode(WIFI_OFF)&&ok;
}
static void toneProgramOff(){
  uint32_t r=(rd32(TONE1)&~(K_MASK|SCALE_MASK|GATE_MASK))|(uint32_t(baseK)&K_MASK)|(uint32_t(askCode(ASK_CW))<<SCALE_SHIFT);
  wr32(TONE1,r);wr32(TONE2,rd32(TONE2)&~GATE_MASK);wr32(TONE3,rd32(TONE3)&~GATE_MASK);memw();
}
static void cwCarrierOn(){
  uint32_t r=(rd32(TONE1)&~(K_MASK|SCALE_MASK|GATE_MASK));
  r|=(uint32_t(baseK+CW_K_OFFSET_STEPS)&K_MASK);
  r|=uint32_t(askCode(ASK_CW))<<SCALE_SHIFT;
  wr32(TONE1,r|GATE_MASK);
  wr32(TONE2,rd32(TONE2)&~GATE_MASK);
  wr32(TONE3,rd32(TONE3)&~GATE_MASK);memw();
}
static bool prepareOfflineRf(){
  // Sequence du moteur B2, meme sans serveur web ni association precedente.
  if(rfOwned&&!releaseOfflineRf())return false;
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true,false);
  WiFi.mode(WIFI_OFF);
  delay(50);
  if(!WiFi.mode(WIFI_STA)){releaseOfflineRf();return false;}
  wifi_station_set_auto_connect(0);
  wifi_station_disconnect();
  WiFi.setAutoReconnect(false);
  if(!wifi_set_sleep_type(NONE_SLEEP_T)){releaseOfflineRf();return false;}
  delay(100);
  if(!wifi_set_channel(RF_CHANNEL)){releaseOfflineRf();return false;}
  delay(40);
  if(wifi_get_channel()!=RF_CHANNEL||WiFi.status()==WL_CONNECTED){releaseOfflineRf();return false;}
  saveRfState();
  enterManual();
  if(!txPathOn()){releaseOfflineRf();return false;}
  carrierQ16=khzToQ16(int32_t(RF_KHZ)-(2407+5*RF_CHANNEL)*1000)+FM_K_OFFSET_STEPS*65536;
  baseK=uint16_t((carrierQ16+0x8000)>>16)&1023u;
  set_ana_scale(selectedApwr);
  toneProgramOff();set_txclk(1);
  return true;
}
static bool rfOff(){
  if(!releaseOfflineRf())rfReady=false;
  return rfReady;
}
static bool rfOnCW(){
  if(!rfReady)return false;
  if(!rfOwned&&!prepareOfflineRf()){rfReady=false;return false;}
  set_ana_scale(selectedApwr);set_txclk(1);cwCarrierOn();rfEnabled=true;
  return true;
}
static void preheatCW(){
  // Le navigateur attend STREAM_PRET, la FIFO UART n'a pas d'audio a recevoir.
  ESP.wdtFeed();
  const uint32_t ps=xt_rsil(15);
  cwCarrierOn();
  const uint32_t begin=cyc();
  uint32_t lastFeed=begin;
  while(uint32_t(cyc()-begin)<CW_PREHEAT_MS*CPU_MHZ*1000UL){
    const uint32_t now=cyc();
    if(uint32_t(now-lastFeed)>=25UL*CPU_MHZ*1000UL){ESP.wdtFeed();lastFeed=now;}
  }
  xt_wsr_ps(ps);memw();
  rfEnabled=true;
}

// Dithering borne original de la version WFM MONO HI-FI 48 kHz.
static int32_t sdErr=0,sdCode=0;
static uint32_t sdLast=0,sdIters=0,sdCycles=0;
static constexpr int32_t sdThr=20<<16;
static IRAM_ATTR __attribute__((noinline)) void ditherFreqOnly(int32_t q,uint32_t endCycle){
  const int32_t kb=(q+0x8000)>>16;
  const int32_t r=q-(kb<<16);
  const uint32_t base=(rd32(TONE1)&~(K_MASK|SCALE_MASK|GATE_MASK))|GATE_MASK;
  const uint32_t kN=base|(uint32_t(kb)&K_MASK);
  const uint32_t kU=base|(uint32_t(kb+1)&K_MASK);
  const uint32_t kD=base|(uint32_t(kb-1)&K_MASK);
  const int32_t thr=sdThr;
  int32_t err=sdErr,code=sdCode;
  uint32_t last=sdLast,n=0;
  const uint32_t ps=xt_rsil(15);
  const uint32_t t0=ESP.getCycleCount();
  do {
    uint32_t now=ESP.getCycleCount();
    if(int32_t(endCycle-now)<180)break;
    uint32_t dt=now-last;
    if(dt>8192u)dt=8192u;
    const int32_t errNow=err+(q-(code<<16))*int32_t(dt);
    if(errNow>thr||errNow<-thr){
      const bool up=errNow>0;
      const int32_t rate=up?65536-r:65536+r;
      int32_t len=(up?errNow:-errNow)/rate;
      int32_t room=int32_t(endCycle-now)-180;
      if(room<0)room=0;
      if(len>room)len=room;
      if(len>360)len=360;
      if(len>0){
        wr32(TONE1,up?kU:kD);memw();
        const uint32_t tp=ESP.getCycleCount();
        dt=tp-last;if(dt>8192u)dt=8192u;
        err+=(q-(code<<16))*int32_t(dt);
        code=up?kb+1:kb-1;
        const int32_t actualRoom=int32_t(endCycle-tp)-120;
        if(actualRoom<len)len=actualRoom;
        if(len>0)while(int32_t(ESP.getCycleCount()-tp)<len){}
        last=tp;
      }
    }
    wr32(TONE1,kN);memw();
    const uint32_t tw=ESP.getCycleCount();
    dt=tw-last;if(dt>8192u)dt=8192u;
    err+=(q-(code<<16))*int32_t(dt);
    err=constrain(err,-(1<<29),1<<29);
    code=kb;last=tw;++n;
  }while(n<8u&&int32_t(last-endCycle)<0);
  xt_wsr_ps(ps);memw();
  sdErr=err;sdCode=code;sdLast=last;
  sdIters+=n;sdCycles+=last-t0;
}

// Comme B2, l'ISR ne lit que des deviations K Q16 deja converties en DRAM.
// L'occupation et les seuils du protocole se comptent en octets UART.
static int32_t audioRing[RING_SAMPLES];
static volatile uint32_t head=0,tail=0;
static volatile uint32_t playedSamples=0;
static volatile uint32_t underruns=0,lateTicks=0,maxLateCycles=0,overruns=0;
static volatile bool timerActive=false,timerFinished=false,timerError=false;
static volatile uint32_t nextDeadline=0,cycleFraction=0;
static uint32_t expectedSamples=0;
static constexpr uint32_t SAMPLE_CYCLES=CPU_MHZ*1000000UL/FM_FS;
static constexpr uint32_t SAMPLE_REMAINDER=(CPU_MHZ*1000000UL)%FM_FS;
static constexpr uint32_t BURST_CYCLES=1450u;
static constexpr uint32_t TIMER_DIV_CYCLES=32u;
static inline uint32_t occupancyBytes(){return (head-tail)*BYTES_PER_SAMPLE;}
static uint32_t savedMacMask=0;
static bool macLoan=false;

static inline int32_t deviationToK(int32_t q4){
  const uint32_t magnitude=uint32_t(q4<0?-q4:q4);
  // WFM2: arrondir magnitude * 4 * 65536 / 78125. Le coefficient Q15
  // tronque donne au plus une unite de moins pour les valeurs valides.
  uint32_t q16=(magnitude*109951u+16384u)>>15;
  // 262144 = 3 * 78125 + 27769. Comparer apres retrait du terme entier
  // pour corriger l'arrondi sans division ni produit 64 bits dans le poll UART.
  if(magnitude*27769u+39062u>=(q16+1u-3u*magnitude)*78125u)++q16;
  return q4<0?-int32_t(q16):int32_t(q16);
}
static inline void IRAM_ATTR silent(){
  volatile uint32_t* tone=reinterpret_cast<volatile uint32_t*>(TONE1);
  *tone=(*tone)&~GATE_MASK;__asm__ volatile("memw" ::: "memory");
}
static void IRAM_ATTR tick(){
  const uint32_t entered=cyc();
  const int32_t late=int32_t(entered-nextDeadline);
  if(late>0){
    if(uint32_t(late)>maxLateCycles)maxLateCycles=uint32_t(late);
    if(late>int32_t(7u*CPU_MHZ))++lateTicks;
  }
  if(!timerActive){silent();timerFinished=true;timer1_disable();return;}
  if(playedSamples>=expectedSamples){
    silent();timerActive=false;timerFinished=true;timer1_disable();return;
  }
  const uint32_t t=tail;
  if(t==head){
    ++underruns;silent();timerError=true;timerActive=false;timerFinished=true;timer1_disable();return;
  }
  const int32_t dq=audioRing[t&RING_MASK];
  tail=t+1u;
  ++playedSamples;
  const uint32_t desiredEnd=cyc()+BURST_CYCLES;
  // Conserver 640 cycles pour quitter le dither et rearmer Timer1.
  const uint32_t safeEnd=nextDeadline+SAMPLE_CYCLES-640u;
  const uint32_t burstEnd=int32_t(desiredEnd-safeEnd)>0?safeEnd:desiredEnd;
  ditherFreqOnly(carrierQ16+dq,burstEnd);
  nextDeadline+=SAMPLE_CYCLES;
  cycleFraction+=SAMPLE_REMAINDER;
  if(cycleFraction>=FM_FS){++nextDeadline;cycleFraction-=FM_FS;}
  const uint32_t after=cyc();
  int32_t remain=int32_t(nextDeadline-after);
  if(remain<320){
    ++overruns;nextDeadline=after+SAMPLE_CYCLES;cycleFraction=0;remain=SAMPLE_CYCLES;
  }
  uint32_t ticks=uint32_t(remain)/TIMER_DIV_CYCLES;
  if(ticks<10u)ticks=10u;
  timer1_write(ticks);
}
static void startFM(){
  const uint32_t ps=xt_rsil(15);
  gateOff();toneProgramOff();
  sdErr=0;sdCode=(carrierQ16+0x8000)>>16;
  sdLast=cyc();sdIters=sdCycles=0;
  savedMacMask=rd32(MAC_INT_ENA);wr32(MAC_INT_ENA,0);memw();macLoan=true;
  playedSamples=underruns=lateTicks=maxLateCycles=overruns=0;
  timer1_disable();timer1_detachInterrupt();timer1_isr_init();timer1_attachInterrupt(tick);
  nextDeadline=cyc()+SAMPLE_CYCLES;cycleFraction=0;
  timerError=false;timerFinished=false;timerActive=true;
  timer1_enable(TIM_DIV16,TIM_EDGE,TIM_SINGLE);
  timer1_write(SAMPLE_CYCLES/TIMER_DIV_CYCLES);
  xt_wsr_ps(ps);memw();
}
static void stopFM(){
  const uint32_t ps=xt_rsil(15);
  timerActive=false;timer1_disable();timer1_detachInterrupt();
  silent();
  if(macLoan){wr32(MAC_INT_ENA,savedMacMask);memw();macLoan=false;}
  xt_wsr_ps(ps);memw();
  // La session UART reste maitre de la PHY hors association entre les clips.
  if(rfEnabled)cwCarrierOn();
}

enum class State:uint8_t { IDLE, CMD_FIELDS, STREAM, FAULT };
static State state=State::IDLE;
static uint8_t commandBuffer[6];
static uint8_t commandPos=0;
static uint8_t startOpcode=0;
static uint32_t commandTime=0;
static bool streamStarted=false;
// Flux non acquitte. Octets effectivement places dans la FIFO/puis RAM.
static uint32_t receivedWireBytes=0;
static uint32_t totalWireBytes=0;
static uint8_t sampleLowByte=0;
static int32_t maxDeviationQ4=0;
static uint32_t lastByteProgressAt=0;
static uint32_t fifoPeakPeriod=0, fifoPeakMax=0, lastFifoTelemetry=0;
static uint32_t lastLoopFeed=0;
static bool ramPaused=false;
static uint32_t lastPauseSentAt=0;
static uint32_t lastResumeSentAt=0;
static uint32_t repeatResumeUntil=0;
static uint32_t lastRamTelemetry=0;
static uint32_t highestRam=0;
static uint8_t lastError=0;
static uint8_t unsentEvent=0; // statut critique a renvoyer si FIFO TX momentanement pleine
static uint32_t quarantineUntil=0;
static void sendEvent(uint8_t event){if(!uartSend(event))unsentEvent=event;}
static uint32_t totalTimeStarted=0;
static void returnIdle(uint8_t reason){
  stopFM();
  receivedWireBytes=0;streamStarted=false;ramPaused=false;
  uartReturnCommands(); // Reprise du driver RX Arduino apres purge FIFO.
  state=State::IDLE;
  if(reason!=ST_FINISH){lastError=reason;}
  sendEvent(reason);
  if(reason!=ST_FINISH)quarantineUntil=millis()+100u;
  // Aucun code 0..100 ne doit etre emis hors streaming.
}
static void abortStream(uint8_t reason){returnIdle(reason);}

static void startStreamFromCommand(){
  const uint32_t samples=uint32_t(commandBuffer[0])|
     (uint32_t(commandBuffer[1])<<8)|(uint32_t(commandBuffer[2])<<16)|
     (uint32_t(commandBuffer[3])<<24);
  const uint8_t devIndex=commandBuffer[4];
  const uint8_t apwr=commandBuffer[5];
  if(startOpcode!=CMD_START||samples<256u||samples>60000000UL||devIndex>2u||apwr>191u||!rfReady||!rfEnabled){
    uartSend(ST_CMD_ERROR);state=State::IDLE;return;
  }
  selectedApwr=apwr;
  if(!prepareOfflineRf()){
    rfReady=false;state=State::FAULT;sendEvent(ST_RF_FAULT);return;
  }
  preheatCW();
  maxDeviationQ4=6250*(int32_t(devIndex)+1);
  head=tail=0;
  expectedSamples=samples;
  totalWireBytes=samples*BYTES_PER_SAMPLE;
  highestRam=0;lastError=0;
  timerFinished=false;timerError=false;
  receivedWireBytes=0;streamStarted=false;ramPaused=false;
  fifoPeakPeriod=fifoPeakMax=0;lastFifoTelemetry=0;
  lastPauseSentAt=lastResumeSentAt=lastRamTelemetry=repeatResumeUntil=0;
  lastByteProgressAt=millis();
  totalTimeStarted=millis();
  uartEnterStream();
  state=State::STREAM;
  // Accuse UNIQUE de transition en STREAM: l HTML commence SEULEMENT alors.
  sendEvent(ST_STREAM_READY);
}
static void commandPoll(){
  // COMMANDES: ISR UART du core active, Serial lit son buffer RX + FIFO.
  // STREAM: cette fonction n'est JAMAIS appelee (sourd aux commandes).
  while(Serial.available()>0){
    const int v=Serial.read();
    if(v<0)break;
    const uint8_t b=uint8_t(v);
    // Meme si la mise en route PHY echoue, PING donne une reponse observable.
    if(state==State::FAULT){sendEvent(ST_RF_FAULT);continue;}
    if(state==State::CMD_FIELDS){
      commandBuffer[commandPos++]=b;
      if(commandPos==sizeof(commandBuffer)){
        state=State::IDLE;startStreamFromCommand();return;
      }
      continue;
    }
    if(b==CMD_PING){sendEvent(rfReady?(rfEnabled?ST_READY:ST_STOP_RF):ST_RF_FAULT);}
    else if(b==CMD_START||b==CMD_LEGACY_START){
      // Lire aussi les six champs d'une ancienne commande, puis la rejeter.
      // Cela evite d'interpreter les champs comme des commandes RF separees.
      startOpcode=b;commandPos=0;state=State::CMD_FIELDS;commandTime=millis();
    }
    else if(b==CMD_RF_OFF){sendEvent(rfOff()?ST_STOP_RF:ST_RF_FAULT);}
    else if(b==CMD_RF_ON){sendEvent(rfOnCW()?ST_READY:ST_RF_FAULT);}
    else sendEvent(ST_CMD_ERROR);
  }
}
static void streamPoll(){
  // Pas d'ISR UART RX: les octets sont lus DIRECTEMENT de la FIFO materielle.
  // Aucun blocage; Timer1 continue son horloge de 48 kHz.
  const uint32_t raw=rd32(UART_INT_RAW);
  if(raw&(UART_RX_OVERFLOW_RAW|UART_RX_FRAME_RAW|UART_RX_PARITY_RAW)){
    abortStream(ST_OVERFLOW);return;
  }
  if(streamStarted&&overruns){abortStream(ST_RF_FAULT);return;}
  if(timerFinished){
    if(timerError||underruns)abortStream(ST_UNDERRUN);
    else returnIdle(ST_FINISH);
    return;
  }

  const uint32_t count=fifoCount();
  if(count>fifoPeakPeriod)fifoPeakPeriod=count;
  if(count>fifoPeakMax)fifoPeakMax=count;
  if(count>0u){
    // Lire TOUS les octets actuellement disponibles, a chaque tour de loop.
    // Un bloc navigateur peut couvrir plusieurs passages dans la FIFO.
    for(uint32_t i=0;i<count;++i){
      const uint8_t b=uint8_t(rd32(UART_FIFO)&0xffu);
      if(receivedWireBytes>=totalWireBytes){abortStream(ST_OVERFLOW);return;}
      if((receivedWireBytes&1u)==0u){
        sampleLowByte=b;
      }else{
        const int16_t q4=int16_t(uint16_t(sampleLowByte)|(uint16_t(b)<<8));
        if(q4<-maxDeviationQ4||q4>maxDeviationQ4){abortStream(ST_CMD_ERROR);return;}
        const uint32_t h=head;
        if(h-tail>=RING_SAMPLES){abortStream(ST_OVERFLOW);return;}
        audioRing[h&RING_MASK]=deviationToK(q4);
        memw();head=h+1u;
      }
      ++receivedWireBytes;
    }
    lastByteProgressAt=millis();
    const uint32_t used=occupancyBytes();
    if(used>highestRam)highestRam=used;
    if(!streamStarted && (used>=START_LEVEL||receivedWireBytes==totalWireBytes)){
      startFM();streamStarted=true;sendEvent(ST_PLAY);
    }
  }

  const uint32_t used=occupancyBytes();
  const uint32_t now=millis();

  // PRIORITE ABSOLUE aux changements de flux : ces octets ne sont PAS des
  // autorisations de blocs. Le navigateur STOPPE toute nouvelle ecriture.
  if(!ramPaused&&used>=HIGH_WATER){
    ramPaused=true;
    lastPauseSentAt=0; // si FIFO TX pleine, reessayer DES LE PROCHAIN tour
    if(uartSend(ST_PAUSE))lastPauseSentAt=now;
  }else if(ramPaused&&used<=LOW_WATER){
    ramPaused=false;
    repeatResumeUntil=now+250u;
    lastResumeSentAt=0;
    if(uartSend(ST_RESUME))lastResumeSentAt=now;
    lastByteProgressAt=now; // ne pas considerer la pause normale comme panne
  }

  // Renvoi idempotent de PAUSE/REPRISE pour pallier un statut perdu.
  // Tester la FIFO TX sans jamais attendre qu'elle se libere.
  if(ramPaused){
    if((lastPauseSentAt==0||uint32_t(now-lastPauseSentAt)>=FLOW_REPEAT_MS)&&uartSend(ST_PAUSE))lastPauseSentAt=now;
  }else if(repeatResumeUntil && int32_t(repeatResumeUntil-now)>0 &&
           (lastResumeSentAt==0||uint32_t(now-lastResumeSentAt)>=FLOW_REPEAT_MS)){
    if(uartSend(ST_RESUME))lastResumeSentAt=now;
  }

  // Periodique, uniquement informatif : 0..100 = pourcentage RAM, 10 Hz.
  // Les messages de controle PAUSE/REPRISE passent toujours en premier.
  if(uint32_t(now-lastRamTelemetry)>=100u){
    const uint8_t percent=uint8_t((used*100u+RAM_SIZE/2u)/RAM_SIZE);
    if(uartSend(percent)){lastRamTelemetry=now;}
  }
  // Pic FIFO sur 250 ms. Cette mesure ne garantit pas un maximum absolu.
  if(uint32_t(now-lastFifoTelemetry)>=250u){
    const uint32_t measured=(fifoPeakPeriod+3u)/4u;
    const uint8_t quart=uint8_t(measured>32u?32u:measured);
    if(uartSend(uint8_t(202u+quart))){
      fifoPeakPeriod=0;lastFifoTelemetry=now;
    }
  }

  // Pendant PAUSE, l'absence de donnees n'est PAS une erreur UART.
  // En lecture FM, l'ISR signale independamment tout underrun.
  if(!ramPaused && receivedWireBytes<totalWireBytes &&
     uint32_t(now-lastByteProgressAt)>UART_TIMEOUT_MS){
    abortStream(ST_TIMEOUT);return;
  }
}
void setup(){
  uartInit();
  if(ESP.getCpuFreqMHz()!=CPU_MHZ){
    sendEvent(ST_RF_FAULT);state=State::FAULT;return;
  }
  rfReady=prepareOfflineRf();
  if(!rfReady||!rfOnCW()){
    sendEvent(ST_RF_FAULT);
    state=State::FAULT;
    return;
  }
  sendEvent(ST_READY);
  lastLoopFeed=millis();
}
void loop(){
  if(unsentEvent&&uartSend(unsentEvent))unsentEvent=0;
  if(state==State::FAULT){commandPoll();ESP.wdtFeed();return;}
  if(state==State::IDLE){
    if(quarantineUntil&&int32_t(millis()-quarantineUntil)<0){
      // Rejeter les octets d un ancien bloc arrive trop tard apres erreur.
      while(Serial.available()>0)(void)Serial.read();
    }else{quarantineUntil=0;commandPoll();}
  }
  else if(state==State::CMD_FIELDS){
    commandPoll();
    if(state==State::CMD_FIELDS&&uint32_t(millis()-commandTime)>UART_TIMEOUT_MS){
      state=State::IDLE;
      // La commande est incomplete; purger le buffer logiciel Arduino + FIFO.
      while(Serial.available()>0)(void)Serial.read();
      sendEvent(ST_TIMEOUT);
    }
  }else if(state==State::STREAM){streamPoll();}
  if(uint32_t(millis()-lastLoopFeed)>=10u){ESP.wdtFeed();lastLoopFeed=millis();}
}
