/*
  SSBReceiverHQ — ESP8266EX, Arduino ESP8266 3.1.2, 160 MHz.
  Receive-only 2.4 GHz IQ for browser SSB demodulation. START takes the
  digital channel center in kHz. The RF LO is nominally 24 kHz lower.
  UART0 1.5 Mbaud 8N2: STATUS, START <kHz> HQ, STOP; idle GAIN AUTO or
  GAIN <RF 0..6> <VGA 0..7>. Any received byte stops an active stream.

  IQ10: 170 bytes, uint32 LE first 48 kS/s complex sample index after the
  marker, 64 packed IQ pairs, CRC-16/CCITT-FALSE LE over index + payload.
  Each five-byte LE group stores two pairs as I0, Q0, I1, Q1 in successive
  10-bit fields. Valid components are ±511. Pair (-512, -512) is an erasure.
  One IQ count equals eight internal Q5 ADC units before limiting.
  A positive-frequency RF offset is positive complex baseband frequency.

  RX PHY tuning, PBUS gain and IQ estimator setup adapted from SP8ESA.
  https://github.com/SP8ESA/ESP8266_2.4GHz_SSB_TRX

  MIT License

  Copyright (c) 2026 SP8ESA

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <stdlib.h>
#include <string.h>
extern "C" {
#include "user_interface.h"
void set_rf_freq_offset(int xtal, int mhz, int off);
extern uint8_t chip6_phy_init_ctrl[];
}

namespace {
constexpr uint32_t cpuHz=160000000, baud=1500000;
constexpr uint32_t iqRate=96000, outputRate=48000, lowIfHz=iqRate/4;
static_assert(iqRate==2*outputRate,"The IQ output decimator is fixed at 2:1");
constexpr uint32_t period=cpuHz/iqRate, remainder=cpuHz%iqRate;
constexpr uint32_t iqCtrl=0x6000057Cu, iqSumI=0x600005DCu, iqSumQ=0x600005E0u;
constexpr uint32_t iqMask=0xFFFA0001u, iqDone=0x80000000u, pbusCmd=0x60000594u;
constexpr uint8_t frameCount=8, samplesPerFrame=64;
constexpr uint16_t frameSize=4+4+5*samplesPerFrame/2+2;
static_assert(samplesPerFrame%4==0,"IQ groups and UART writes must stay aligned");
constexpr int16_t invalidIq=-512;
constexpr int32_t iqFullScale=511;

const int16_t channelTaps[]={-1, 2, 5, -1, -11, -5, 18, 19, -19, -42, 5, 67, 29, -83, -88, 72, 163, -13, -233, -106, 265, 288, -213, -518, 21, 764, 399, -986, -1308, 1139, 5063, 7000};
constexpr uint8_t halfTaps=sizeof(channelTaps)/sizeof(channelTaps[0])-1;
constexpr uint8_t filterLength=2*halfTaps+1;

struct Stats {
  uint32_t captured, skipped, timeouts, samples, sent, invalid, queueDrops, maxLate;
  uint32_t peakIq, clippedIq, clippedAdc;
};
struct Queue {
  uint8_t head, tail, fill;
  uint16_t crc, sentBytes;
};

uint8_t frames[frameCount][frameSize];
uint16_t crcTable[256];
int32_t historyI[128], historyQ[128];
bool phyReady=false;
int gainRf=0, gainVga=0;

static inline __attribute__((always_inline)) uint32_t cycles() {
  uint32_t result;
  asm volatile("rsr %0, ccount" : "=a"(result));
  return result;
}

static inline __attribute__((always_inline)) uint32_t rd(uint32_t address) {
  return *reinterpret_cast<volatile uint32_t*>(address);
}

static inline __attribute__((always_inline)) void wr(uint32_t address,uint32_t value) {
  *reinterpret_cast<volatile uint32_t*>(address)=value;
}

static inline __attribute__((always_inline)) uint16_t crcByte(uint16_t crc,uint8_t value) {
  return (crc<<8)^crcTable[(crc>>8)^value];
}

static void prepareCrc() {
  for (uint16_t i=0;i<256;++i) {
    uint16_t crc=i<<8;
    for (uint8_t bit=0;bit<8;++bit) crc=(crc<<1)^((crc&0x8000u)?0x1021u:0u);
    crcTable[i]=crc;
  }
}

static inline __attribute__((always_inline)) void pushOutput(
    Queue& queue,Stats& stats,uint32_t index,int16_t i,int16_t q) {
  if (i==invalidIq) ++stats.invalid;
  const uint8_t next=(queue.head+1)&(frameCount-1);
  if (!queue.fill && next==queue.tail) {
    ++stats.queueDrops;
    return;
  }
  uint8_t* frame=frames[queue.head];
  if (!queue.fill) {
    frame[0]='I'; frame[1]='Q'; frame[2]='1'; frame[3]='0';
    queue.crc=0xffffu;
    for (uint8_t i=0;i<4;++i) {
      frame[4+i]=index>>(8*i);
      queue.crc=crcByte(queue.crc,frame[4+i]);
    }
  }
  const uint32_t packed=(static_cast<uint16_t>(i)&0x3ffu) |
                        ((static_cast<uint16_t>(q)&0x3ffu)<<10);
  const uint16_t offset=8+5*queue.fill/2;
  if (queue.fill&1u) {
    // The previous pair occupies the low nibble of the shared byte.
    frame[offset]|=packed<<4;
    frame[offset+1]=packed>>4;
    frame[offset+2]=packed>>12;
    queue.crc=crcByte(crcByte(crcByte(queue.crc,frame[offset]),frame[offset+1]),frame[offset+2]);
  } else {
    frame[offset]=packed;
    frame[offset+1]=packed>>8;
    frame[offset+2]=packed>>16;
    queue.crc=crcByte(crcByte(queue.crc,frame[offset]),frame[offset+1]);
  }
  if (++queue.fill==samplesPerFrame) {
    frame[frameSize-2]=queue.crc;
    frame[frameSize-1]=queue.crc>>8;
    queue.head=next;
    queue.fill=0;
  }
}

static inline __attribute__((always_inline)) void pumpUart(Queue& queue,Stats& stats) {
  if (queue.tail==queue.head || ((USS(0)>>USTXC)&0xff)>=118) return;
  // Up to two bytes per acquisition tick sustain 127.5 kB/s of framed IQ.
  const uint8_t* data=frames[queue.tail]+queue.sentBytes;
  USF(0)=data[0]; USF(0)=data[1];
  queue.sentBytes+=2;
  if (queue.sentBytes==frameSize) {
    queue.sentBytes=0;
    queue.tail=(queue.tail+1)&(frameCount-1);
    stats.sent+=samplesPerFrame;
  }
}

static inline __attribute__((always_inline)) void pushIq(
    uint8_t& pos,uint8_t& available,int32_t i,int32_t q) {
  pos=(pos+1)&63;
  historyI[pos]=historyI[pos+64]=i;
  historyQ[pos]=historyQ[pos+64]=q;
  if (available<filterLength) ++available;
}

static uint32_t tune(uint32_t khz) {
  const uint8_t channel=constrain((static_cast<int32_t>(khz)-2407000+2500)/5000,1,13);
  const int mhz=2407+5*channel;
  const int32_t delta=static_cast<int32_t>(khz)-mhz*1000;
  const int32_t off=(delta*1024+(delta<0?-500:500))/1000;
  if (!wifi_set_channel(channel)) return 0;
  delay(40);
  if (wifi_get_channel()!=channel) return 0;
  set_rf_freq_offset(chip6_phy_init_ctrl[1],mhz,off);
  return mhz*1000000LL+off*1000000LL/1024;
}

static bool startPhy() {
  WiFi.persistent(false);
  if (!WiFi.mode(WIFI_STA)) return false;
  wifi_station_set_auto_connect(0);
  wifi_station_set_reconnect_policy(false);
  wifi_station_disconnect();
  if (!wifi_set_sleep_type(NONE_SLEEP_T)) return false;
  delay(100);
  return true;
}

static void setReceiveGain(int rf,int vga) {
  using Call=void(*)(void);
  if (rf<0) {
    if (rd(pbusCmd)&1u) reinterpret_cast<Call>(0x40007648u)();
    return;
  }
  using Read=uint32_t(*)(uint32_t,uint32_t);
  using Force=void(*)(uint32_t,uint32_t,uint32_t);
  static const uint8_t rfTable[]={0x00,0x40,0x60,0x70,0x78,0x7c,0x7f};
  reinterpret_cast<Call>(0x4000737cu)();
  const auto read=reinterpret_cast<Read>(0x400074d8u);
  const auto force=reinterpret_cast<Force>(0x4000747cu);
  force(3,1,rfTable[rf]);
  force(3,2,(read(3,2)&0x1c7u)|(static_cast<uint32_t>(vga)<<3));
}

static void IRAM_ATTR runReceiver(Stats& stats) {
  uint32_t deadline=cycles()+160, fraction=0, sampleIndex=0, priorIndex=0;
  uint32_t maxLate=0, watchdog=0;
  int32_t peak=0;
  uint8_t phase=0, priorPhase=0, pos=0, historyCount=0;
  bool haveSlot=false, captured=false, previousOutput=false, badPair=false;
  int32_t i=0,q=0,holdI=0,holdQ=0;
  int32_t iqDcI=0,iqDcQ=0;
  Queue queue={};

  wr(iqCtrl,rd(iqCtrl)|1u);
  // Average 256 ADC points (6.4 us); the capture overlaps CPU processing.
  const uint32_t estimator=(rd(iqCtrl)&(iqMask&~iqDone))|(255u<<2)|1u;

  while (!((USS(0)>>USRXC)&0xff)) {
    while (static_cast<int32_t>(cycles()-deadline)<0) {}
    const uint32_t now=cycles();
    if (now-deadline>period/3) {
      if (haveSlot) pushIq(pos,historyCount,holdI,holdQ);
      if (previousOutput) {
        pushOutput(queue,stats,priorIndex,invalidIq,invalidIq);
        previousOutput=false;
      }
      do {
        ++stats.skipped;
        pushIq(pos,historyCount,holdI,holdQ);
        phase=(phase+1)&3;
        if (!(phase&1)) pushOutput(queue,stats,sampleIndex++,invalidIq,invalidIq);
        deadline+=period;
        fraction+=remainder;
        if (fraction>=iqRate) { fraction-=iqRate; ++deadline; }
      } while (static_cast<int32_t>(now-deadline)>=0);
      haveSlot=false;
      badPair=true;
      continue;
    }
    const uint32_t late=now-deadline;
    if (late>maxLate) maxLate=late;
    wr(iqCtrl,estimator|2u);

    if (haveSlot) {
      if (!previousOutput) badPair=false;
      if (captured) {
        // Remove receiver I/Q offsets before quarter-rate low-IF translation.
        iqDcI+=(i*4096-iqDcI)>>10;
        iqDcQ+=(q*4096-iqDcQ)>>10;
        const int32_t ci=i-(iqDcI>>12), cq=q-(iqDcQ>>12);
        switch (priorPhase) {
          case 0: holdI=ci; holdQ=cq; break;
          case 1: holdI=cq; holdQ=-ci; break;
          case 2: holdI=-ci; holdQ=-cq; break;
          default: holdI=-cq; holdQ=ci; break;
        }
      } else {
        badPair=true;
      }
      pushIq(pos,historyCount,holdI,holdQ);
      if (previousOutput) {
        int16_t outputI=invalidIq, outputQ=invalidIq;
        if (!badPair && historyCount==filterLength) {
          const int32_t* newestI=&historyI[pos+64];
          const int32_t* newestQ=&historyQ[pos+64];
          const int32_t* oldestI=newestI-(filterLength-1);
          const int32_t* oldestQ=newestQ-(filterLength-1);
          int32_t fi=0,fq=0;
          for (uint8_t tap=0;tap<halfTaps;++tap) {
            const int32_t k=channelTaps[tap];
            fi+=k*(*oldestI++ + *newestI--);
            fq+=k*(*oldestQ++ + *newestQ--);
          }
          fi=(fi+channelTaps[halfTaps]*(*newestI))>>14;
          fq=(fq+channelTaps[halfTaps]*(*newestQ))>>14;
          const int32_t scaledI=(fi+4-(fi<0))>>3;
          const int32_t scaledQ=(fq+4-(fq<0))>>3;
          const int32_t level=max(abs(scaledI),abs(scaledQ));
          peak=max(peak,level);
          if (level>iqFullScale) ++stats.clippedIq;
          outputI=constrain(scaledI,-iqFullScale,iqFullScale);
          outputQ=constrain(scaledQ,-iqFullScale,iqFullScale);
        }
        pushOutput(queue,stats,priorIndex,outputI,outputQ);
      }
    }

    pumpUart(queue,stats);
    if (++watchdog==16384) {
      ESP.wdtFeed();
      watchdog=0;
    }
    while (!(rd(iqCtrl)&iqDone) && cycles()-now<period*3) {}
    captured=rd(iqCtrl)&iqDone;
    if (captured) {
      // Estimator sum has six fractional bits. A 256-point average after
      // shifting nine bits retains five fractional ADC bits.
      i=static_cast<int32_t>(rd(iqSumI))>>9;
      q=static_cast<int32_t>(rd(iqSumQ))>>9;
      if (i<-14000||i>14000||q<-14000||q>14000) {
        ++stats.clippedAdc;
        i=i<-14000?-14000:i>14000?14000:i;
        q=q<-14000?-14000:q>14000?14000:q;
      }
    } else {
      ++stats.timeouts;
    }
    wr(iqCtrl,estimator);
    haveSlot=true;
    priorPhase=phase;
    phase=(phase+1)&3;
    previousOutput=!(phase&1);
    if (previousOutput) priorIndex=sampleIndex++;
    deadline+=period;
    fraction+=remainder;
    if (fraction>=iqRate) { fraction-=iqRate; ++deadline; }
  }

  wr(iqCtrl,(rd(iqCtrl)&iqMask&~1u)|0x1000u);
  const uint32_t flush=cycles();
  while (queue.tail!=queue.head && cycles()-flush<cpuHz/20) pumpUart(queue,stats);
  stats.samples=sampleIndex;
  stats.captured=sampleIndex*2+(phase&1)-stats.skipped-stats.timeouts;
  stats.maxLate=maxLate;
  stats.peakIq=peak;
}

static bool number(const char* text,unsigned long& value) {
  if (!text || !*text) return false;
  char* end=nullptr;
  value=strtoul(text,&end,10);
  return !*end;
}

static void command(char* line) {
  char* verb=strtok(line," \t");
  if (!verb) return;
  if (strcmp(verb,"STATUS")==0) {
    Serial.printf("OK STATUS SSBReceiverHQ 1 %u %lu %ld\n",ESP.getCpuFreqMHz(),
                  static_cast<unsigned long>(baud),static_cast<long>(iqFullScale));
    return;
  }
  if (strcmp(verb,"STOP")==0) {
    Serial.println("OK STOP");
    return;
  }
  if (strcmp(verb,"GAIN")==0) {
    char* rf=strtok(nullptr," \t");
    char* vga=strtok(nullptr," \t");
    unsigned long rfValue,vgaValue;
    if (rf && strcmp(rf,"AUTO")==0 && !vga) {
      gainRf=-1;
      Serial.println("OK GAIN AUTO");
    } else if (number(rf,rfValue) && number(vga,vgaValue) && rfValue<=6 && vgaValue<=7 &&
               !strtok(nullptr," \t")) {
      gainRf=rfValue;
      gainVga=vgaValue;
      Serial.printf("OK GAIN %d %d\n",gainRf,gainVga);
    } else {
      Serial.println("ERR GAIN");
    }
    return;
  }
  if (strcmp(verb,"START")!=0) {
    Serial.println("ERR COMMAND");
    return;
  }
  unsigned long khz;
  char* frequency=strtok(nullptr," \t");
  const char* mode=strtok(nullptr," \t");
  if (!number(frequency,khz) || khz<2400000 || khz>2483500 || strtok(nullptr," \t")) {
    Serial.println("ERR FREQUENCY");
    return;
  }
  if (mode && strcmp(mode,"HQ")!=0) {
    Serial.println("ERR MODE");
    return;
  }
  if (ESP.getCpuFreqMHz()!=160) {
    Serial.println("ERR CPU160");
    return;
  }
  if (!phyReady) {
    Serial.println("ERR PHY");
    return;
  }
  const uint32_t lo=tune(khz-lowIfHz/1000);
  if (!lo) {
    Serial.println("ERR TUNE");
    return;
  }
  setReceiveGain(gainRf,gainVga);
  Stats stats={};
  Serial.printf("OK START %lu %lu HQ\n",static_cast<unsigned long>(lo+lowIfHz),
                static_cast<unsigned long>(outputRate));
  Serial.flush();
  const uint32_t ps=xt_rsil(15);
  runReceiver(stats);
  xt_wsr_ps(ps);
  Serial.flush();
  delay(10);
  while (Serial.available()) Serial.read();
  setReceiveGain(-1,0);
  Serial.printf("\nEND samples=%lu sent=%lu iq=%lu skipped_iq=%lu timeouts=%lu invalid=%lu queue_drops=%lu max_late=%lu lo_hz=%lu peak_iq=%lu clipped_iq=%lu clipped_adc=%lu\n",
                static_cast<unsigned long>(stats.samples),static_cast<unsigned long>(stats.sent),
                static_cast<unsigned long>(stats.captured),static_cast<unsigned long>(stats.skipped),
                static_cast<unsigned long>(stats.timeouts),static_cast<unsigned long>(stats.invalid),
                static_cast<unsigned long>(stats.queueDrops),static_cast<unsigned long>(stats.maxLate),
                static_cast<unsigned long>(lo),static_cast<unsigned long>(stats.peakIq),
                static_cast<unsigned long>(stats.clippedIq),static_cast<unsigned long>(stats.clippedAdc));
}
}  // namespace

void setup() {
  Serial.begin(baud,SERIAL_8N2);
  Serial.setDebugOutput(false);
  prepareCrc();
  phyReady=startPhy();
}

void loop() {
  static char line[48];
  static uint8_t length=0;
  static bool overflow=false;
  while (Serial.available()) {
    const char c=Serial.read();
    if (c=='\r') continue;
    if (c=='\n') {
      if (overflow) Serial.println("ERR COMMAND");
      else {
        line[length]='\0';
        command(line);
      }
      length=0;
      overflow=false;
    } else if (length<sizeof(line)-1) {
      line[length++]=c;
    } else {
      overflow=true;
    }
  }
}
