/*
  B2_SSB_LIVE_SERIAL_HIFI - SSB USB/LSB en Web Serial
  Transport UART0 FLOW8K du projet B2 AM et modulation polaire SP8ESA
  (https://github.com/SP8ESA/ESP8266_2.4GHz_SSB_TRX).

  ESP8266 a 160 MHz, PHY hors association Wi-Fi. Le CW a K SSB + 1
  demarre des que la PHY est prete et maintient le prechauffage entre clips.
  UART0 2 Mbauds 8N1. START 0xA8 puis nombre d'echantillons u32 LE,
  cadence (0: 24 kHz, 1: 32 kHz), APWR (0..191), bande (0: USB, 1: LSB),
  et prechauffage u16 LE en secondes (1..900). Tous les PLAY prechauffent
  a K SSB + 1, avec retour CW entre clips. RF OFF (0xA2) annule le CW.
  0xAA annule le PLAY pendant le prechauffage et maintient le CW.
  Etat 237: prechauffage; attendre 239 avant le flux.
  0xA9 -> 250 identifie ce firmware avant le premier START.
  Chaque echantillon: ASK Q8.8 u16 LE, puis delta K Q16 i16 LE.
  LSB inverse le signe du delta K. ASK 0 = crête, 0x8000 = gate ferme.
  Respecter PAUSE 235 et REPRISE 236; blocs UART de 704 octets au maximum.
  0xA6 lit les diagnostics en commandes: 249, puis huit u32 LE
  (lus, retards >2 us, retard max cycles, sautes, overruns, underruns,
  pic FIFO RX, derniere erreur).

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
static constexpr uint32_t SAMPLE_RATES[]={24000u,32000u};
static constexpr uint16_t MAX_ASK_Q8=127u*256u;
static constexpr uint16_t ASK_GATE_OFF=0x8000u;
static constexpr uint32_t RAM_SIZE=8192UL;
static constexpr uint32_t BYTES_PER_SAMPLE=4u;
static constexpr uint32_t RING_SAMPLES=RAM_SIZE/BYTES_PER_SAMPLE;
static constexpr uint32_t RING_MASK=RING_SAMPLES-1u;
static constexpr uint32_t START_LEVEL=6144UL; // 48 ms de precharge a 32 kHz
static constexpr uint32_t HIGH_WATER=7168UL; // 87.5 %, 1024 octets de reserve
static constexpr uint32_t LOW_WATER=5120UL;  // 62.5 %, reprise hysteresis
static_assert((RING_SAMPLES & RING_MASK)==0u,"RING_SAMPLES doit etre une puissance de deux");
static_assert(LOW_WATER < START_LEVEL && START_LEVEL < HIGH_WATER,"Seuils incompatibles");
static_assert(HIGH_WATER+704u<RAM_SIZE,"Reserve FIFO/RAM insuffisante");
static constexpr uint32_t UART_TIMEOUT_MS=1000UL;
static constexpr uint32_t FLOW_REPEAT_MS=12UL; // 40 ms de reserve au seuil bas a 32 kHz
static constexpr uint8_t ASK_CW=0;
// Meme porteuse de reference que le projet AM. Un pas K vaut 78 125 Hz.
static constexpr int32_t SSB_K_OFFSET_STEPS=4;
static constexpr uint16_t CW_K_OFFSET_STEPS=1u;
static constexpr uint16_t PREHEAT_MIN_SECONDS=1u;
static constexpr uint16_t PREHEAT_MAX_SECONDS=900u;
static constexpr uint8_t APWR_DEFAULT=63;

// ESP -> PC: 0..100 % RAM INFORMATIF (aucun credit !)
// 202..234: pic FIFO quantifie par 4 octets (diagnostic uniquement)
// 235: PAUSE OBLIGATOIRE, 236: REPRISE, 239: STREAM PRET
// 240..248: etats et erreurs preexistants.
static constexpr uint8_t ST_PAUSE=235;
static constexpr uint8_t ST_RESUME=236;
static constexpr uint8_t ST_PREHEAT=237;
static constexpr uint8_t ST_STREAM_READY=239;
static constexpr uint8_t ST_READY=240;      // prechauffage, commandes disponibles
static constexpr uint8_t ST_PLAY=241;       // SSB active
static constexpr uint8_t ST_FINISH=242;     // dernier echantillon lu, retour CW
static constexpr uint8_t ST_TIMEOUT=243;    // Source serie absente trop longtemps
static constexpr uint8_t ST_UNDERRUN=244;   // donnees RAM epuisees
static constexpr uint8_t ST_OVERFLOW=245;   // vraie erreur FIFO materielle
static constexpr uint8_t ST_RF_FAULT=246;   // echec RF ou cadence critique
static constexpr uint8_t ST_CMD_ERROR=247;  // commande incorrecte
static constexpr uint8_t ST_STOP_RF=248;    // RF arretee sur ordre, securite
static constexpr uint8_t ST_SSB_IDENTITY=250;
// PC -> ESP uniquement en mode COMMANDES (ISR UART RX active) :
static constexpr uint8_t CMD_PING=0xA0;
static constexpr uint8_t CMD_LEGACY_START=0xA1;
static constexpr uint8_t CMD_MONO_START=0xA4;
static constexpr uint8_t CMD_STEREO_START=0xA5;
static constexpr uint8_t CMD_AM_START=0xA7;
static constexpr uint8_t CMD_START=0xA8;
static constexpr uint8_t CMD_IDENTITY=0xA9;
static constexpr uint8_t CMD_CANCEL_PREHEAT=0xAA;
static constexpr uint8_t CMD_DIAGNOSTICS=0xA6; // [0xF9, 8 compteurs uint32 LE] en commandes
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

// Registres RF du moteur B2, sans Wi-Fi associe.
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
static bool rfReady=false;
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
  carrierQ16=khzToQ16(int32_t(RF_KHZ)-(2407+5*RF_CHANNEL)*1000)+SSB_K_OFFSET_STEPS*65536;
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
  set_ana_scale(selectedApwr);set_txclk(1);cwCarrierOn();
  return true;
}
// Timer1 impose la cadence audio. Entre deux IRQ la boucle remplit l'anneau
// depuis la FIFO UART. Le dernier code K est maintenu pendant ce temps et
// l'erreur de phase correspondante est rattrapee au prochain tick.
struct PolarSample { uint16_t envelope; int16_t deltaK; };
static_assert(sizeof(PolarSample)==BYTES_PER_SAMPLE,"Echantillon polaire de quatre octets");
static PolarSample audioRing[RING_SAMPLES];
static volatile uint32_t head=0,tail=0;
static volatile uint32_t playedSamples=0;
static volatile uint32_t underruns=0,lateTicks=0,maxLateCycles=0,overruns=0;
static volatile bool timerActive=false,timerFinished=false;
static volatile uint32_t nextDeadline=0,cycleFraction=0;
static uint32_t expectedSamples=0;
static bool lsbMode=false;
static uint32_t timerRate=32000u,sampleCycles=0,sampleRemainder=0;
static constexpr uint32_t TIMER_DIV_CYCLES=2u; // TIM_DIV1, APB 80 MHz
static constexpr int32_t REARM_GUARD_CYCLES=224;
static constexpr uint32_t DITHER_GUARD_CYCLES=1150u; // service FIFO, environ 7,2 us
static constexpr int32_t PHASE_THRESHOLD=20 * 65536;
static constexpr int32_t PHASE_LIMIT=1 << 29;
static inline uint32_t occupancyBytes(){return (head-tail)*BYTES_PER_SAMPLE;}
static uint32_t savedMacMask=0;
static bool macLoan=false;

static uint32_t tonePrefix=0;
static int32_t phaseError=0,activeK=0,previousTarget=0;
static uint32_t lastToneCycle=0;
static uint16_t askError=0;
static bool phaseActive=false;

static inline void IRAM_ATTR silent(){
  volatile uint32_t* tone=reinterpret_cast<volatile uint32_t*>(TONE1);
  *tone=(*tone)&~GATE_MASK;__asm__ volatile("memw" ::: "memory");
}
static inline void IRAM_ATTR integratePhase(int32_t target,uint32_t now){
  const uint32_t elapsed=now-lastToneCycle;
  const int32_t dt=int32_t(elapsed>8192u?8192u:elapsed);
  const int32_t error=phaseError+(target-activeK*65536)*dt;
  phaseError=error>PHASE_LIMIT?PHASE_LIMIT:(error < -PHASE_LIMIT ? -PHASE_LIMIT : error);
  lastToneCycle=now;
}
static inline void IRAM_ATTR writeTone(int32_t target,int32_t code,uint32_t bits){
  wr32(TONE1,bits|(uint32_t(code)&K_MASK));memw();
  integratePhase(target,cyc());
  activeK=code;
}
static inline uint32_t IRAM_ATTR amplitudeBits(uint16_t envelope){
  const uint32_t accumulated=uint32_t(envelope)+askError;
  askError=uint16_t(accumulated&255u);
  const uint8_t ask=uint8_t(accumulated>>8);
  return tonePrefix|(uint32_t(uint8_t(0u-ask))<<SCALE_SHIFT);
}
static void IRAM_ATTR __attribute__((noinline))
emitPolar(const PolarSample& sample,uint32_t sampleAt,uint32_t burstEnd){
  if(sample.envelope==ASK_GATE_OFF){
    silent();phaseActive=false;return;
  }
  const int32_t target=carrierQ16+(lsbMode?-int32_t(sample.deltaK):int32_t(sample.deltaK));
  const int32_t nearest=(target+0x8000)>>16;
  const uint32_t start=cyc();
  if(phaseActive){
    const uint32_t boundary=int32_t(sampleAt-start)>0?start:sampleAt;
    if(int32_t(boundary-lastToneCycle)>0)integratePhase(previousTarget,boundary);
    integratePhase(target,start);
  }else{
    phaseError=0;askError=0;lastToneCycle=start;
    activeK=nearest;phaseActive=true;
  }
  previousTarget=target;
  // Methode SP8ESA: integrer l'erreur de K puis la compenser par une courte
  // impulsion K+1 ou K-1. Borner l'impulsion au budget RF de cet echantillon.
  do{
    const uint32_t bits=amplitudeBits(sample.envelope);
    const uint32_t now=cyc();
    integratePhase(target,now);
    const int32_t budget=int32_t(burstEnd-now)-160;
    if(budget>80&&(phaseError>PHASE_THRESHOLD||phaseError < -PHASE_THRESHOLD)){
      const int32_t direction=phaseError>0?1:-1;
      const int32_t correction=nearest+direction;
      const int32_t slope=direction*(correction*65536-target);
      int32_t hold=direction*phaseError/slope;
      if(hold>budget)hold=budget;
      writeTone(target,correction,bits);
      const uint32_t until=lastToneCycle+uint32_t(hold);
      while(int32_t(until-cyc())>0){}
    }
    writeTone(target,nearest,bits);
  }while(int32_t(burstEnd-cyc())>160);
}
static inline void IRAM_ATTR advanceDeadline(){
  nextDeadline+=sampleCycles;
  cycleFraction+=sampleRemainder;
  if(cycleFraction>=timerRate){++nextDeadline;cycleFraction-=timerRate;}
}
static void IRAM_ATTR finishTimer(){
  silent();timerActive=false;timerFinished=true;timer1_disable();
}
static void IRAM_ATTR tick(){
  const uint32_t sampleAt=nextDeadline;
  const int32_t late=int32_t(cyc()-sampleAt);
  if(late>0){
    if(uint32_t(late)>maxLateCycles)maxLateCycles=uint32_t(late);
    if(late>int32_t(2u*CPU_MHZ))++lateTicks;
  }
  if(!timerActive||playedSamples>=expectedSamples){finishTimer();return;}
  const uint32_t t=tail;
  if(t==head){++underruns;finishTimer();return;}
  advanceDeadline();
  const int32_t remain=int32_t(nextDeadline-cyc());
  if(remain<REARM_GUARD_CYCLES){++overruns;finishTimer();return;}
  timer1_write(uint32_t(remain)/TIMER_DIV_CYCLES);
  const PolarSample sample=audioRing[t&RING_MASK];
  tail=t+1u;
  ++playedSamples;
  emitPolar(sample,sampleAt,nextDeadline-DITHER_GUARD_CYCLES);
}
static void startSSB(){
  const uint32_t ps=xt_rsil(15);
  gateOff();toneProgramOff();
  tonePrefix=(rd32(TONE1)&~(K_MASK|SCALE_MASK|GATE_MASK))|GATE_MASK;
  phaseActive=false;
  savedMacMask=rd32(MAC_INT_ENA);wr32(MAC_INT_ENA,0);memw();macLoan=true;
  timer1_disable();timer1_detachInterrupt();timer1_isr_init();timer1_attachInterrupt(tick);
  cycleFraction=sampleRemainder;
  timerFinished=false;timerActive=true;
  timer1_enable(TIM_DIV1,TIM_EDGE,TIM_SINGLE);
  nextDeadline=cyc()+sampleCycles;
  timer1_write(sampleCycles/TIMER_DIV_CYCLES);
  xt_wsr_ps(ps);memw();
}
static void stopSSB(){
  const uint32_t ps=xt_rsil(15);
  timerActive=false;timer1_disable();timer1_detachInterrupt();
  silent();
  if(macLoan){wr32(MAC_INT_ENA,savedMacMask);memw();macLoan=false;}
  xt_wsr_ps(ps);memw();
}

enum class State:uint8_t { IDLE, CMD_FIELDS, PREHEAT, STREAM, FAULT };
static State state=State::IDLE;
static uint8_t commandBuffer[9];
static uint32_t preheatUntil=0;
static uint8_t commandPos=0;
static uint8_t startOpcode=0;
static uint32_t commandTime=0;
static bool streamStarted=false;
// Flux non acquitte. Octets effectivement places dans la FIFO/puis RAM.
static uint32_t receivedWireBytes=0;
static uint32_t totalWireBytes=0;
static uint32_t wireSample=0;
static uint32_t lastByteProgressAt=0;
static uint32_t fifoPeakPeriod=0, fifoPeakMax=0, lastFifoTelemetry=0;
static uint32_t lastLoopFeed=0;
static bool ramPaused=false;
static uint32_t lastPauseSentAt=0;
static uint32_t lastResumeSentAt=0;
static uint32_t repeatResumeUntil=0;
static uint32_t lastRamTelemetry=0;
static uint8_t lastError=0;
static uint8_t unsentEvent=0; // statut critique a renvoyer si FIFO TX momentanement pleine
static uint32_t quarantineUntil=0;
static void sendEvent(uint8_t event){if(!uartSend(event))unsentEvent=event;}
static void returnIdle(uint8_t reason){
  stopSSB();
  if(reason==ST_FINISH||reason==ST_TIMEOUT||reason==ST_UNDERRUN||
     reason==ST_OVERFLOW||reason==ST_CMD_ERROR){cwCarrierOn();}
  else if(!rfOff()){state=State::FAULT;}
  receivedWireBytes=0;streamStarted=false;ramPaused=false;
  uartReturnCommands(); // Reprise du driver RX Arduino apres purge FIFO.
  if(state!=State::FAULT)state=State::IDLE;
  if(reason!=ST_FINISH){lastError=reason;}
  sendEvent(reason);
  if(reason!=ST_FINISH)quarantineUntil=millis()+100u;
  // Aucun code 0..100 ne doit etre emis hors streaming.
}
static void abortStream(uint8_t reason){returnIdle(reason);}

static void sendSsbDiagnostics(){
  const uint32_t fields[]={playedSamples,lateTicks,maxLateCycles,0u,
                           overruns,underruns,fifoPeakMax,lastError};
  uint8_t frame[1+sizeof(fields)];
  frame[0]=249u;
  for(size_t i=0;i<sizeof(fields)/sizeof(fields[0]);++i){
    for(size_t j=0;j<4u;++j)frame[1u+4u*i+j]=uint8_t(fields[i]>>(8u*j));
  }
  Serial.write(frame,sizeof(frame));
}

static void startStreamFromCommand(){
  const uint32_t samples=uint32_t(commandBuffer[0])|
     (uint32_t(commandBuffer[1])<<8)|(uint32_t(commandBuffer[2])<<16)|
     (uint32_t(commandBuffer[3])<<24);
  const uint8_t rateIndex=commandBuffer[4];
  const uint8_t apwr=commandBuffer[5];
  const uint8_t sideband=commandBuffer[6];
  const uint16_t seconds=uint16_t(commandBuffer[7])|(uint16_t(commandBuffer[8])<<8);
  if(samples<256u||samples>60000000UL||
     rateIndex>=sizeof(SAMPLE_RATES)/sizeof(SAMPLE_RATES[0])||apwr>191u||sideband>1u||
     seconds<PREHEAT_MIN_SECONDS||seconds>PREHEAT_MAX_SECONDS||!rfReady){
    sendEvent(ST_CMD_ERROR);return;
  }
  selectedApwr=apwr;
  if(!rfOnCW()){
    state=State::FAULT;sendEvent(ST_RF_FAULT);return;
  }
  lsbMode=sideband==1u;
  timerRate=SAMPLE_RATES[rateIndex];
  sampleCycles=CPU_MHZ*1000000u/timerRate;
  sampleRemainder=(CPU_MHZ*1000000u)%timerRate;
  head=tail=0;
  expectedSamples=samples;
  totalWireBytes=samples*BYTES_PER_SAMPLE;
  lastError=0;
  playedSamples=underruns=lateTicks=maxLateCycles=overruns=0;
  timerFinished=false;
  wireSample=0;
  receivedWireBytes=0;streamStarted=false;ramPaused=false;
  fifoPeakPeriod=fifoPeakMax=0;lastFifoTelemetry=0;
  lastPauseSentAt=lastResumeSentAt=lastRamTelemetry=repeatResumeUntil=0;
  preheatUntil=millis()+uint32_t(seconds)*1000u;
  state=State::PREHEAT;
  sendEvent(ST_PREHEAT);
}
static void preheatPoll(){
  while(Serial.available()>0){
    const int command=Serial.read();
    if(command==CMD_RF_OFF){
      const bool stopped=rfOff();
      state=stopped?State::IDLE:State::FAULT;
      sendEvent(stopped?ST_STOP_RF:ST_RF_FAULT);
      return;
    }
    if(command==CMD_CANCEL_PREHEAT){
      state=State::IDLE;
      sendEvent(ST_FINISH);
      return;
    }
    if(command==CMD_PING)sendEvent(ST_PREHEAT);
  }
  if(int32_t(millis()-preheatUntil)<0)return;
  lastByteProgressAt=millis();
  uartEnterStream();
  state=State::STREAM;
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
      if(commandPos==(startOpcode==CMD_START?sizeof(commandBuffer):6u)){
        state=State::IDLE;
        if(startOpcode==CMD_START)startStreamFromCommand();
        else sendEvent(ST_CMD_ERROR);
        return;
      }
      continue;
    }
    if(b==CMD_PING||b==CMD_CANCEL_PREHEAT){sendEvent(rfReady?(rfOwned?ST_READY:ST_STOP_RF):ST_RF_FAULT);}
    else if(b==CMD_IDENTITY){sendEvent(rfReady?ST_SSB_IDENTITY:ST_RF_FAULT);}
    else if(b==CMD_DIAGNOSTICS){sendSsbDiagnostics();}
    else if(b==CMD_START||b==CMD_AM_START||b==CMD_STEREO_START||b==CMD_MONO_START||b==CMD_LEGACY_START){
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
  // Aucun blocage; Timer1 conserve sa cadence pendant le remplissage.
  const uint32_t raw=rd32(UART_INT_RAW);
  if(raw&(UART_RX_OVERFLOW_RAW|UART_RX_FRAME_RAW|UART_RX_PARITY_RAW)){
    abortStream(ST_OVERFLOW);return;
  }
  if(streamStarted&&overruns){abortStream(ST_RF_FAULT);return;}
  if(timerFinished){
    if(underruns)abortStream(ST_UNDERRUN);
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
      const uint32_t byteIndex=receivedWireBytes&3u;
      if(byteIndex==0u)wireSample=b;
      else wireSample|=uint32_t(b)<<(8u*byteIndex);
      if(byteIndex==3u){
        const uint16_t envelope=uint16_t(wireSample);
        if(envelope>MAX_ASK_Q8&&envelope!=ASK_GATE_OFF){
          abortStream(ST_CMD_ERROR);return;
        }
        const uint32_t h=head;
        if(h-tail>=RING_SAMPLES){abortStream(ST_OVERFLOW);return;}
        audioRing[h&RING_MASK]={envelope,int16_t(wireSample>>16)};
        memw();head=h+1u;
      }
      ++receivedWireBytes;
    }
    lastByteProgressAt=millis();
    const uint32_t used=occupancyBytes();
    if(!streamStarted && (used>=START_LEVEL||receivedWireBytes==totalWireBytes)){
      startSSB();streamStarted=true;sendEvent(ST_PLAY);
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
  // En lecture SSB, l'ISR signale independamment tout underrun.
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
  }else if(state==State::PREHEAT){preheatPoll();}
  else if(state==State::STREAM){streamPoll();}
  if(uint32_t(millis()-lastLoopFeed)>=10u){ESP.wdtFeed();lastLoopFeed=millis();}
}
