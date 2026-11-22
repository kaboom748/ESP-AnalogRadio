/* SPDX-License-Identifier: GPL-3.0-or-later
 * CB FM stereo PCM12 and V13 wire compatibility for ESP32-C6.
 * Upstream: kaboom748/ESP-AnalogRadio, commit8470a9f7dcd2846648696e850763509127bd3541.
 * Original firmware contains SP8ESA's MIT notice, retained in NOTICE.
 */
#include <Arduino.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "driver/uart.h"
#include "hal/uart_ll.h"
#include "soc/uart_struct.h"
#include "soc/uart_reg.h"
#include "esp_timer.h"
#include "esp_cpu.h"
#include "c6_radio.h"
#include "c6_rx.h"
#include "c6_tx.h"
#include "c6_tx_radio.h"
#include "pcm12_wire.h"
#include "c6_pcm_tx_dsp.h"
#ifndef CB13_BENCH_GAIN
#define CB13_BENCH_GAIN 0
#endif

namespace {
constexpr uint32_t CpuHz=160000000,Baud=1500000,StartLevel=4096;
constexpr uint32_t WireBytesPerSecond=Baud/10u;
constexpr uint32_t MinFrequency=2401000000u,MaxFrequency=2482500000u;
constexpr uint32_t InputTimeoutMs=150,ArmTimeoutMs=1200;
constexpr unsigned Payload=128,Header=10,Frame=138,RxSlots=8;
constexpr uint32_t PcmDspCycles=325,PcmPrepareCycles=200,PcmFinishCycles=200;
constexpr uint32_t PcmUartCycles=155,PcmUartPulseCycles=185,PcmCreditCycles=500;
constexpr uint32_t PcmUartByteCycles=32,PcmStartQuietCycles=CpuHz/5000;
constexpr unsigned PcmTxBurst=4,PcmTxUrgent=8;
constexpr unsigned PcmRxPollSlots=4,PcmRxBurst=4;
constexpr uint32_t UartErrors=UART_RXFIFO_OVF_INT_RAW_M|UART_FRM_ERR_INT_RAW_M|UART_PARITY_ERR_INT_RAW_M;
const char *const ProfileNames[]={"HQ","ROBUST","MONO"};
const uint32_t ProfileRates[]={120000,96000,48000};
uint32_t frequency=2442312500u;
int32_t fine=0;
unsigned profile=0;
uint8_t apwr=63;
bool receiveRequested=false,receiving=false,transmitting=false;
bool pcmRx=false,pcmTx=false;
bool controlPending=false;
uint8_t controlByte=0;
DRAM_ATTR uint8_t rxQueue[RxSlots][pcm12::RxFrameSize];
uint16_t rxRead=0,rxWrite=0,rxFill=0,rxSend=0;
uint32_t rxUartDeadline=0;
uint32_t rxUartRemainder=0;
unsigned pcmRxPoll=0;
struct WireStats {uint32_t bytes=0,frames=0,drops=0,maxQueue=0,maxFifo=0;};
WireStats wire,lastWire;
c6_rx_stats_t lastRx={};
c6_tx_stats_t lastTx={};
struct TxStats {uint32_t received=0,accepted=0,dropped=0,overflow=0,frame=0,parity=0,reserved=0;};
TxStats tx,lastTxWire;
bool txReceived=false;
uint32_t txLastCycles=0,txWatch=0;
uint64_t txArmedAt=0,txLastReady=0;
DRAM_ATTR pcm12::Ingress pcmIngress;
bool pcmSampleStaged=false;
uint32_t pcmMaxRender=0,pcmMaxHotRender=0,pcmMaxPrepare=0,pcmMaxFinish=0;

inline uint32_t IRAM_ATTR cycles() {return esp_cpu_get_cycle_count();}
inline unsigned IRAM_ATTR rxFrameSize() {return pcmRx?pcm12::RxFrameSize:Frame;}
void writeBytes(const void *data,size_t size) {
    const uint8_t *p=static_cast<const uint8_t*>(data);
    while(size) {
        unsigned room=uart_ll_get_txfifo_len(&UART0);
        if(!room)continue;
        const unsigned n=size<room?size:room;
        uart_ll_write_txfifo(&UART0,p,n);p+=n;size-=n;
    }
}
void line(const char *format,...) {
    char data[512];va_list args;va_start(args,format);
    int length=vsnprintf(data,sizeof(data),format,args);va_end(args);
    if(length>0)writeBytes(data,size_t(length)<sizeof(data)?length:sizeof(data)-1);
}
void println(const char *text) {line("%s\r\n",text);}
void flush() {while(!uart_ll_is_tx_idle(&UART0)) {}}
void flushRx() {uart_ll_rxfifo_rst(&UART0);uart_ll_clr_intsts_mask(&UART0,UINT32_MAX);}
bool initializeUart() {
    uart_config_t cfg={};cfg.baud_rate=Baud;cfg.data_bits=UART_DATA_8_BITS;
    cfg.parity=UART_PARITY_DISABLE;cfg.stop_bits=UART_STOP_BITS_1;
    cfg.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;cfg.source_clk=UART_SCLK_PLL_F80M;
    if(uart_param_config(UART_NUM_0,&cfg)!=ESP_OK ||
       uart_set_pin(UART_NUM_0,16,17,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)!=ESP_OK)return false;
    // No Arduino Serial ISR or USB CDC competes for command/stream bytes.
    uart_ll_disable_intr_mask(&UART0,UINT32_MAX);flushRx();return true;
}

void stopRx() {
    if(!receiving)return;
    c6_rx_stop();c6_rx_get_stats(&lastRx);
    // Finish a frame already on the wire, preserving the next text/READY boundary.
    if(rxSend && rxRead!=rxWrite) {
        const unsigned remaining=rxFrameSize()-rxSend;
        writeBytes(rxQueue[rxRead]+rxSend,remaining);
        wire.bytes+=remaining;++wire.frames;
    }
    lastWire=wire;receiving=false;rxRead=rxWrite=rxFill=rxSend=0;flush();
}
bool startRx() {
    if(!receiveRequested || transmitting)return false;
    stopRx();
    const esp_err_t started=pcmRx?c6_rx_start_pcm(frequency,fine,profile):c6_rx_start(frequency,fine,profile);
    if(started!=ESP_OK) {
        receiveRequested=false;c6_radio_idle();println("ERR RX_RF");return false;
    }
    rxRead=rxWrite=rxFill=rxSend=0;wire=WireStats();rxUartDeadline=cycles();rxUartRemainder=0;
    pcmRxPoll=0;receiving=true;
    line("OK RX %lu %lu %s\n",(unsigned long)c6_radio_frequency_hz(),
        (unsigned long)(pcmRx?pcm12::RxRate:ProfileRates[profile]),ProfileNames[profile]);flush();return true;
}
uint8_t *IRAM_ATTR commitPcmRx(void *) {
    const unsigned next=(rxWrite+1)&(RxSlots-1);
    if(next==rxRead) {
        ++wire.drops;
    } else {
        rxWrite=next;
        const unsigned queued=(rxWrite+RxSlots-rxRead)&(RxSlots-1);
        if(queued>wire.maxQueue)wire.maxQueue=queued;
    }
    return rxQueue[rxWrite];
}
void IRAM_ATTR appendRx(uint32_t index,uint8_t value,uint16_t signal,void *) {
    if(!rxFill) {
        if(((rxWrite+1)&(RxSlots-1))==rxRead) {++wire.drops;return;}
        uint8_t *frame=rxQueue[rxWrite];frame[0]='M';frame[1]='P';frame[2]='X';frame[3]='3';
        for(unsigned i=0;i<4;++i)frame[4+i]=index>>(8*i);
        frame[8]=signal;frame[9]=signal>>8;
    }
    rxQueue[rxWrite][Header+rxFill++]=value;
    if(rxFill==Payload) {
        rxFill=0;rxWrite=(rxWrite+1)&(RxSlots-1);
        const unsigned queued=(rxWrite+RxSlots-rxRead)&(RxSlots-1);
        if(queued>wire.maxQueue)wire.maxQueue=queued;
    }
}
bool IRAM_ATTR pumpRx(void *) {
    if(pcmRx && (++pcmRxPoll & (PcmRxPollSlots-1)))return true;
    if(uart_ll_get_rxfifo_len(&UART0)) {
        uart_ll_read_rxfifo(&UART0,&controlByte,1);controlPending=true;return false;
    }
    if(pcmRx) {
        if(rxRead==rxWrite)return true;
        const unsigned room=uart_ll_get_txfifo_len(&UART0);
        if(UART_LL_FIFO_DEF_LEN-room>wire.maxFifo)wire.maxFifo=UART_LL_FIFO_DEF_LEN-room;
        const unsigned remaining=pcm12::RxFrameSize-rxSend;
        const unsigned burst=remaining<PcmRxBurst?remaining:PcmRxBurst;
        const unsigned size=room<burst?room:burst;
        if(!size)return true;
        uart_ll_write_txfifo(&UART0,&rxQueue[rxRead][rxSend],size);
        rxSend+=size;wire.bytes+=size;
        if(rxSend==pcm12::RxFrameSize) {rxSend=0;rxRead=(rxRead+1)&(RxSlots-1);++wire.frames;}
        return true;
    }
    const uint32_t now=cycles();
    if(rxRead==rxWrite) {rxUartDeadline=now;rxUartRemainder=0;return true;}
    if(int32_t(now-rxUartDeadline)<0)return true;
    const unsigned room=uart_ll_get_txfifo_len(&UART0);
    if(UART_LL_FIFO_DEF_LEN-room>wire.maxFifo)wire.maxFifo=UART_LL_FIFO_DEF_LEN-room;
    if(!room)return true;
    uart_ll_write_txfifo(&UART0,&rxQueue[rxRead][rxSend],1);
    ++rxSend;++wire.bytes;rxUartDeadline+=CpuHz/WireBytesPerSecond;
    rxUartRemainder+=CpuHz%WireBytesPerSecond;
    if(rxUartRemainder>=WireBytesPerSecond) {rxUartRemainder-=WireBytesPerSecond;++rxUartDeadline;}
    if(rxSend==Frame) {rxSend=0;rxRead=(rxRead+1)&(RxSlots-1);++wire.frames;}
    return true;
}
void runRx() {
    if(pcmRx)c6_rx_run_pcm(rxQueue[rxWrite],commitPcmRx,pumpRx,nullptr);
    else c6_rx_run(appendRx,pumpRx,nullptr);
    if(controlPending)stopRx();
}

void IRAM_ATTR txByte(uint8_t byte) {
    ++tx.received;txReceived=true;txLastCycles=cycles();
    if(pcmTx) {pcmIngress.push(byte);return;}
    if(byte==0x80) {byte=0;++tx.reserved;}
    if(c6_tx_feed(&byte,1)==1)++tx.accepted;else ++tx.dropped;
}
void IRAM_ATTR readPcmUart(unsigned waiting,uint32_t budget,uint32_t minimum) {
    const unsigned affordable=1+(budget-minimum)/PcmUartByteCycles;
    unsigned count=waiting<affordable?waiting:affordable;
    if(count>PcmTxBurst)count=PcmTxBurst;
    if(pcmIngress.error==pcm12::TxError::None && pcmIngress.magicBytes==4) {
        const unsigned left=pcm12::TxBodySize-pcmIngress.bodyBytes;
        if(count>left)count=left;
        uint8_t *destination=pcmIngress.frames[pcmIngress.write&pcm12::IngressMask]+pcmIngress.bodyBytes;
        uart_ll_read_rxfifo(&UART0,destination,count);
        pcmIngress.receivedBody(count);
    } else {
        uint8_t bytes[PcmTxBurst];
        uart_ll_read_rxfifo(&UART0,bytes,count);
        for(unsigned n=0;n<count;++n)pcmIngress.push(bytes[n]);
    }
    tx.received+=count;txReceived=true;txLastCycles=cycles();
}
void IRAM_ATTR renderPcmTx(uint32_t budget=UINT32_MAX) {
    if(pcmIngress.read==pcmIngress.write || !c6_tx_free_samples())return;
    const uint8_t *frame=pcmIngress.frames[pcmIngress.read&pcm12::IngressMask];
    const uint32_t started=cycles();
    if(!c6_pcm_tx_dsp::pairPending())
        c6_pcm_tx_dsp::loadPair(frame+4+3*pcmIngress.pair);
    if(!pcmSampleStaged && budget<PcmDspCycles) {
        c6_pcm_tx_dsp::prepareSample();
        pcmSampleStaged=true;
        const uint32_t elapsed=cycles()-started;
        if(elapsed>pcmMaxPrepare)pcmMaxPrepare=elapsed;
        return;
    }
    const uint8_t *envelope=frame+4+pcm12::TxAudioBytes;
    const bool completingStage=pcmSampleStaged;
    const int16_t mpx=completingStage?c6_pcm_tx_dsp::finishSample(envelope):c6_pcm_tx_dsp::renderSample(envelope);
    pcmSampleStaged=false;
    tx.accepted+=c6_tx_enqueue_pcm12(mpx);
    if(!c6_pcm_tx_dsp::pairPending() && ++pcmIngress.pair==pcm12::TxPairs) {
        pcmIngress.pair=0;
        ++pcmIngress.read;
    }
    const uint32_t elapsed=cycles()-started;
    if(elapsed>pcmMaxRender)pcmMaxRender=elapsed;
    if(budget!=UINT32_MAX && elapsed>pcmMaxHotRender)pcmMaxHotRender=elapsed;
    if(completingStage && elapsed>pcmMaxFinish)pcmMaxFinish=elapsed;
}
void IRAM_ATTR publishPcmCredit() {
    if(!pcmIngress.flow || !pcmIngress.ready)return;
    const uint32_t now=cycles();
    if(uint32_t(now-pcmIngress.lastCheck)<CpuHz/1000u)return;
    pcmIngress.lastCheck=now;
    const uint32_t limit=pcmIngress.credit(c6_tx_pcm_played());
    if(limit-pcmIngress.granted<pcm12::CreditStep &&
       uint32_t(now-pcmIngress.lastGrant)<CpuHz/20u)return;
    if(uart_ll_get_txfifo_len(&UART0)<25)return;
    uint8_t data[25]={'O','K',' ','P','C','M',' ','C','R','E','D','I','T',' '};
    char digits[10];unsigned n=0;uint32_t remaining=limit;
    do {digits[n++]=char('0'+remaining%10);remaining/=10;}while(remaining);
    unsigned cursor=14;
    while(n)data[cursor++]=digits[--n];
    data[cursor++]='\n';
    uart_ll_write_txfifo(&UART0,data,cursor);
    pcmIngress.granted=limit;pcmIngress.lastGrant=now;
}
void IRAM_ATTR pumpTx(void *) {
    if(!pcmTx) {
        if(uart_ll_get_rxfifo_len(&UART0)) {
            uint8_t byte;uart_ll_read_rxfifo(&UART0,&byte,1);txByte(byte);
        }
    } else {
        const bool onPulse=c6_tx_pcm_in_pulse();
        const uint32_t minimum=onPulse?PcmUartPulseCycles:PcmUartCycles;
        uint32_t budget=c6_tx_pcm_pump_budget();
        unsigned waiting=budget>=minimum?uart_ll_get_rxfifo_len(&UART0):0;
        if(waiting>=PcmTxUrgent) {
            readPcmUart(waiting,budget,minimum);
            waiting=0;
        }
        if(pcmIngress.error!=pcm12::TxError::None) {c6_tx_stop();return;}
        if(pcmIngress.read!=pcmIngress.write) {
            budget=c6_tx_pcm_pump_budget();
            if(budget>=(pcmSampleStaged?PcmFinishCycles:PcmPrepareCycles))renderPcmTx(budget);
        }
        if(waiting && (budget=c6_tx_pcm_pump_budget())>=minimum)readPcmUart(waiting,budget,minimum);
        if(onPulse)return;
        if(pcmIngress.flow && pcmIngress.ready &&
           uint32_t(cycles()-pcmIngress.lastCheck)>=CpuHz/1000u &&
           c6_tx_pcm_pump_budget()>=PcmCreditCycles)publishPcmCredit();
    }
    if((++txWatch&63u)!=0)return;
    const uint32_t errors=uart_ll_get_intraw_mask(&UART0)&UartErrors;
    if(errors) {
        if(errors&UART_RXFIFO_OVF_INT_RAW_M)++tx.overflow;
        if(errors&UART_FRM_ERR_INT_RAW_M)++tx.frame;
        if(errors&UART_PARITY_ERR_INT_RAW_M)++tx.parity;
        uart_ll_clr_intsts_mask(&UART0,errors);
        if(pcmTx) {pcmIngress.error=pcm12::TxError::Uart;c6_tx_stop();return;}
    }
    if(uint32_t(cycles()-txLastCycles)>=CpuHz/1000u*InputTimeoutMs)c6_tx_stop();
}
void finishTx() {
    c6_tx_stop();c6_tx_get_stats(&lastTx);lastTxWire=tx;
    const char *fault=pcmTx?pcm12::errorText(pcmIngress.error):nullptr;
    if(!fault)fault=c6_tx_fault();
    transmitting=false;flushRx();
    if(fault) {
        line("ERR %s\n",fault);
        const int64_t until=esp_timer_get_time()+120000;
        do {flushRx();delay(0);}while(esp_timer_get_time()<until);
    }
    if(pcmTx) {
        line("OK PCM TX frames=%lu input=%lu peak=%lu missed=%lu underrun=%lu tail=%lu fifo=%lu protocol=%u minq=%lu maxpair=%lu\n",
             (unsigned long)pcmIngress.read,(unsigned long)pcmIngress.write,(unsigned long)pcmIngress.peak,
             (unsigned long)lastTx.timer_skips,(unsigned long)(lastTx.underruns-lastTx.pcm_tail_underruns),
             (unsigned long)lastTx.pcm_tail_underruns,(unsigned long)(tx.overflow+tx.frame+tx.parity),
             pcmIngress.error==pcm12::TxError::None?0u:1u,(unsigned long)lastTx.min_queue,(unsigned long)pcmMaxRender);
        line("OK PCM WORK hot=%lu late=%lu prepare=%lu finish=%lu pulse=%lu key=%lu\n",
             (unsigned long)pcmMaxHotRender,(unsigned long)lastTx.max_late_cycles,
             (unsigned long)pcmMaxPrepare,(unsigned long)pcmMaxFinish,
             (unsigned long)lastTx.max_pulse_late_cycles,(unsigned long)lastTx.startup_cycles);
    }
    // Original CB13 resumes the selected RX profile after the PCM stream ends.
    if(receiveRequested)startRx();
}
void beginTx(uint8_t requested,bool pcm=false,bool flow=false) {
    if(!receiveRequested) {println("ERR SELECT_RX");return;}
    stopRx();
    if(!(pcm?c6_tx_start_pcm(frequency,requested):c6_tx_start(frequency,requested))) {
        println("ERR TX_RF");startRx();return;
    }
    pcmTx=pcm;
    if(pcmTx) {
        pcmIngress.reset(flow);c6_pcm_tx_dsp::reset();pcmSampleStaged=false;
        pcmMaxRender=pcmMaxHotRender=pcmMaxPrepare=pcmMaxFinish=0;
    }
    apwr=requested;tx=TxStats();txReceived=false;txWatch=0;
    static const uint8_t boundary[pcm12::RxFrameSize]={};
    writeBytes(boundary,pcmTx?sizeof(boundary):Frame);flush();
    flushRx();transmitting=true;txArmedAt=esp_timer_get_time();txLastReady=0;txLastCycles=cycles();
}
void pollTx() {
    const unsigned count=uart_ll_get_rxfifo_len(&UART0);
    for(unsigned n=0;n<count;++n) {uint8_t byte;uart_ll_read_rxfifo(&UART0,&byte,1);txByte(byte);}
    if(pcmTx) {
        if(pcmIngress.error!=pcm12::TxError::None){finishTx();return;}
        renderPcmTx();
    }
    const uint64_t now=esp_timer_get_time();
    if(!txReceived && (!txLastReady || now-txLastReady>=25000)) {
        const uint8_t ready[]={0xef,0xef,0xef,0xef};writeBytes(ready,sizeof(ready));txLastReady=now;
        if(pcmTx)pcmIngress.ready=true;
    }
    if(pcmTx)publishPcmCredit();
    const bool wireQuiet=!pcmTx || !pcmIngress.flow || uint32_t(cycles()-txLastCycles)>=PcmStartQuietCycles;
    if(c6_tx_queued_samples()>=StartLevel && wireQuiet) {
        flush();c6_tx_run(pumpTx,nullptr);finishTx();return;
    }
    if((!txReceived && now-txArmedAt>=ArmTimeoutMs*1000u) ||
       (txReceived && uint32_t(cycles()-txLastCycles)>=CpuHz/1000u*InputTimeoutMs))finishTx();
}

bool parseUnsigned(const char *text,uint32_t &value) {
    if(!text || !*text)return false;
    uint64_t number=0;
    for(const char *p=text;*p;++p) {
        if(*p<'0'||*p>'9')return false;
        number=number*10+unsigned(*p-'0');if(number>UINT32_MAX)return false;
    }
    value=number;return true;
}
bool parseFine(const char *text,int32_t &value) {
    if(!text||!*text)return false;
    bool negative=*text=='-';if(negative||*text=='+')++text;
    uint32_t number;if(!parseUnsigned(text,number)||number>50000)return false;
    value=negative?-int32_t(number):int32_t(number);return true;
}
void diagnostics() {
    line("OK DIAG RX n=%lu inv=%lu skip=%lu tout=%lu drop=%lu late=%lu\n",
      (unsigned long)lastRx.samples,(unsigned long)lastRx.invalid,(unsigned long)lastRx.skipped,
      (unsigned long)lastRx.timeouts,(unsigned long)lastWire.drops,(unsigned long)lastRx.max_late_cycles);
    line("OK DIAG TX n=%lu under=%lu zero=%lu skip=%lu drop=%lu ov=%lu frame=%lu parity=%lu late=%lu\n",
      (unsigned long)lastTx.samples,(unsigned long)lastTx.underruns,(unsigned long)lastTx.zero_samples,
      (unsigned long)lastTx.timer_skips,(unsigned long)lastTxWire.dropped,
      (unsigned long)lastTxWire.overflow,(unsigned long)lastTxWire.frame,(unsigned long)lastTxWire.parity,
      (unsigned long)lastTx.max_late_cycles);
    line("OK DIAG C6EST adc_bits=10 window=%lu source_hz=%lu iq_hz=%lu cycles_mean=%lu cycles_max=%lu profiling=%u\n",
      (unsigned long)lastRx.iq_window_samples,(unsigned long)lastRx.source_rate_hz,(unsigned long)lastRx.iq_rate_hz,
      (unsigned long)lastRx.mean_cycles_per_iq,(unsigned long)lastRx.max_read_cycles,(unsigned)C6_RX_PROFILE);
    line("OK DIAG RXRF peak_adc=%lu pcm_clipped=%lu unwraps=%lu cfo=%ld dc_i=%ld dc_q=%ld min_hz=%ld max_hz=%ld\n",
      (unsigned long)lastRx.peak,(unsigned long)lastRx.pcm_clipped,(unsigned long)lastRx.fm_unwraps,
      (long)lastRx.frequency_hz,(long)lastRx.dc_i_adc,(long)lastRx.dc_q_adc,
      (long)lastRx.discriminator_min_hz,(long)lastRx.discriminator_max_hz);
    line("OK DIAG RXGAIN updates=%lu index=%lu maximum=%lu\n",
      (unsigned long)lastRx.gain_updates,(unsigned long)lastRx.gain_index,(unsigned long)lastRx.gain_maximum);
    uint16_t rfGain,bbGain,attenuation;c6_tx_radio_power(&rfGain,&bbGain,&attenuation);
    line("OK DIAG TXRF rf=%u bb=%u attenuation=%u\n",(unsigned)rfGain,(unsigned)bbGain,(unsigned)attenuation);
    line("OK DIAG END\n");
}
void command(char *text) {
    char *save=nullptr;const char *name=strtok_r(text," \t",&save);if(!name)return;
    const char *value=strtok_r(nullptr," \t",&save);
    if(!value && !strcmp(name,"STATUS")) {println("OK STATUS CB_FM_STEREO 4");return;}
    if(!value && !strcmp(name,"CAPS")) {println("OK CAPS PCM12");println("OK PCM FLOW");return;}
    if(!value && !strcmp(name,"CAPABILITIES?")) {println("OK CAPABILITIES RX=2.4 TX=2.4");return;}
    if(!value && !strcmp(name,"STOP")) {receiveRequested=false;stopRx();c6_radio_idle();println("OK STOP");return;}
    if(!value && !strcmp(name,"DIAG")) {diagnostics();return;}
#if CB13_BENCH_GAIN
    if(!strcmp(name,"GAIN")&&value) {c6_radio_set_rx_gain(atoi(value));println("OK GAIN");return;}
#endif
    const bool flow=!strcmp(name,"TXFLOW");
    const bool transmitPcm=flow||!strcmp(name,"TXPCM");
    if(transmitPcm||!strcmp(name,"TX")) {
        uint32_t requested;
        if(!parseUnsigned(value,requested)||requested>191||strtok_r(nullptr," \t",&save)) {println("ERR TX_APWR");return;}
        beginTx(uint8_t(requested),transmitPcm,flow);return;
    }
    const bool receivePcm=!strcmp(name,"RXPCM");
    if((!receivePcm&&strcmp(name,"RX"))||!value) {println("ERR COMMAND");return;}
    uint32_t selected;
    if(!parseUnsigned(value,selected)) {println("ERR FREQUENCY");return;}
    if(selected>=1 && selected<=13)selected=(2407u+5u*selected)*1000000u+312500u;
    if(selected<MinFrequency||selected>MaxFrequency) {println("ERR FREQUENCY");return;}
    const char *mode=strtok_r(nullptr," \t",&save);unsigned chosen=3;
    for(unsigned n=0;n<3;++n)if(mode&&!strcmp(mode,ProfileNames[n]))chosen=n;
    if(chosen==3) {println("ERR MODE");return;}
    const char *offset=strtok_r(nullptr," \t",&save);int32_t selectedFine=0;
    if((offset&&!parseFine(offset,selectedFine))||strtok_r(nullptr," \t",&save)) {println("ERR FINE");return;}
    frequency=selected;profile=chosen;fine=selectedFine;pcmRx=receivePcm;receiveRequested=true;
}
} // namespace

extern "C" void cb_fm_stereo_run() {
    if(!initializeUart() || !c6_tx_init())for(;;)delay(1000);
    char text[48]={};size_t used=0;bool invalid=false;int64_t lastInput=0;
    for(;;) {
        if(transmitting) {pollTx();continue;}
        uint8_t byte=0;bool got=false;
        if(controlPending) {byte=controlByte;controlPending=false;got=true;}
        else if(uart_ll_get_rxfifo_len(&UART0)) {uart_ll_read_rxfifo(&UART0,&byte,1);got=true;}
        if(got) {
            if(receiving)stopRx();
            lastInput=esp_timer_get_time();
            if(byte==0xa5 && !used && !invalid) {beginTx(apwr);continue;}
            if(byte=='\r')continue;
            if(byte=='\n') {
                if(invalid)println("ERR COMMAND");else {text[used]=0;command(text);}
                used=0;invalid=false;
            } else if((byte=='\t'||(byte>=32&&byte<=126))&&used<sizeof(text)-1)text[used++]=byte;
            else invalid=true;
        } else if(used||invalid) {
            if(esp_timer_get_time()-lastInput<120000)continue;
            used=0;invalid=false;
        } else if(receiveRequested) {
            if(receiving)runRx();else startRx();
        } else delay(1);
    }
}
