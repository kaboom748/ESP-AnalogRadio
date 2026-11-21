/* SPDX-License-Identifier: GPL-3.0-or-later
 * CB_FM_STEREOv13_Arduino wire compatibility for ESP32-C5.
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
#include "c5_radio.h"
#include "c5_rx.h"
#include "c5_tx.h"
#include "c5_tx_radio.h"
#include "c5_bands.h"
#ifndef CB13_BENCH_GAIN
#define CB13_BENCH_GAIN 0
#endif

namespace {
constexpr uint32_t CpuHz=240000000,Baud=1500000,StartLevel=4096;
constexpr uint32_t InputTimeoutMs=150,ArmTimeoutMs=1200;
constexpr unsigned Payload=128,Header=10,Frame=138,RxSlots=8;
constexpr uint32_t UartErrors=UART_RXFIFO_OVF_INT_RAW_M|UART_FRM_ERR_INT_RAW_M|UART_PARITY_ERR_INT_RAW_M;
const char *const ProfileNames[]={"HQ","ROBUST","MONO"};
const uint32_t ProfileRates[]={120000,96000,48000};
uint64_t frequency=UINT64_C(2442312500);
int32_t fine=0;
unsigned profile=0;
uint8_t apwr=63;
bool receiveRequested=false,receiving=false,transmitting=false;
bool controlPending=false;
uint8_t controlByte=0;
DRAM_ATTR uint8_t rxQueue[RxSlots][Frame];
uint16_t rxRead=0,rxWrite=0,rxFill=0,rxSend=0;
uint32_t rxUartDeadline=0;
struct WireStats {uint32_t bytes=0,frames=0,drops=0,maxQueue=0,maxFifo=0;};
WireStats wire,lastWire;
c5_rx_stats_t lastRx={};
c5_tx_stats_t lastTx={};
struct TxStats {uint32_t received=0,accepted=0,dropped=0,overflow=0,frame=0,parity=0,reserved=0;};
TxStats tx,lastTxWire;
bool txReceived=false;
uint32_t txLastCycles=0,txWatch=0;
uint64_t txArmedAt=0,txLastReady=0;

inline uint32_t IRAM_ATTR cycles() {uint32_t c;asm volatile("rdcycle %0":"=r"(c));return c;}
inline bool before(uint32_t a,uint32_t b) {return int32_t(a-b)<0;}
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
    cfg.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;cfg.source_clk=UART_SCLK_XTAL;
    if(uart_param_config(UART_NUM_0,&cfg)!=ESP_OK ||
       uart_set_pin(UART_NUM_0,11,12,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)!=ESP_OK)return false;
    // No Arduino Serial ISR or USB CDC competes for command/stream bytes.
    uart_ll_disable_intr_mask(&UART0,UINT32_MAX);flushRx();return true;
}

void stopRx() {
    if(!receiving)return;
    c5_rx_stop();c5_rx_get_stats(&lastRx);
    // Finish a frame already on the wire, preserving the next text/READY boundary.
    if(rxSend && rxRead!=rxWrite) {
        writeBytes(rxQueue[rxRead]+rxSend,Frame-rxSend);
        wire.bytes+=Frame-rxSend;++wire.frames;
    }
    lastWire=wire;receiving=false;rxRead=rxWrite=rxFill=rxSend=0;flush();
}
bool startRx() {
    if(!receiveRequested || transmitting)return false;
    stopRx();
    if(c5_rx_start(frequency,fine,profile)!=ESP_OK) {
        receiveRequested=false;c5_radio_idle();println("ERR RX_RF");return false;
    }
    rxRead=rxWrite=rxFill=rxSend=0;wire=WireStats();rxUartDeadline=cycles();receiving=true;
    line("OK RX %llu %lu %s\n",(unsigned long long)c5_radio_frequency_hz(),
        (unsigned long)ProfileRates[profile],ProfileNames[profile]);flush();return true;
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
    if(uart_ll_get_rxfifo_len(&UART0)) {
        uart_ll_read_rxfifo(&UART0,&controlByte,1);controlPending=true;return false;
    }
    const uint32_t now=cycles();
    if(rxRead==rxWrite) {rxUartDeadline=now;return true;}
    if(int32_t(now-rxUartDeadline)<0)return true;
    const unsigned room=uart_ll_get_txfifo_len(&UART0);
    if(UART_LL_FIFO_DEF_LEN-room>wire.maxFifo)wire.maxFifo=UART_LL_FIFO_DEF_LEN-room;
    if(!room)return true;
    uart_ll_write_txfifo(&UART0,&rxQueue[rxRead][rxSend],1);
    ++rxSend;++wire.bytes;rxUartDeadline+=CpuHz/150000u;
    if(rxSend==Frame) {rxSend=0;rxRead=(rxRead+1)&(RxSlots-1);++wire.frames;}
    return true;
}
void runRx() {c5_rx_run(appendRx,pumpRx,nullptr);if(controlPending)stopRx();}

void IRAM_ATTR txByte(uint8_t byte) {
    ++tx.received;txReceived=true;txLastCycles=cycles();
    if(byte==0x80) {byte=0;++tx.reserved;}
    if(c5_tx_feed(&byte,1)==1)++tx.accepted;else ++tx.dropped;
}
void IRAM_ATTR pumpTx(void *) {
    if(uart_ll_get_rxfifo_len(&UART0)) {
        uint8_t byte;uart_ll_read_rxfifo(&UART0,&byte,1);txByte(byte);
    }
    if((++txWatch&63u)!=0)return;
    const uint32_t errors=uart_ll_get_intraw_mask(&UART0)&UartErrors;
    if(errors) {
        if(errors&UART_RXFIFO_OVF_INT_RAW_M)++tx.overflow;
        if(errors&UART_FRM_ERR_INT_RAW_M)++tx.frame;
        if(errors&UART_PARITY_ERR_INT_RAW_M)++tx.parity;
        uart_ll_clr_intsts_mask(&UART0,errors);
    }
    if(uint32_t(cycles()-txLastCycles)>=CpuHz/1000u*InputTimeoutMs)c5_tx_stop();
}
void finishTx() {
    c5_tx_stop();c5_tx_get_stats(&lastTx);lastTxWire=tx;
    const char *fault=c5_tx_fault();
    transmitting=false;flushRx();
    if(fault) {
        line("ERR %s\n",fault);
        const int64_t until=esp_timer_get_time()+120000;
        do {flushRx();delay(0);}while(esp_timer_get_time()<until);
    }
    // Original CB13 resumes the selected RX profile after the PCM stream ends.
    if(receiveRequested)startRx();
}
void beginTx(uint8_t requested) {
    if(!receiveRequested) {println("ERR SELECT_RX");return;}
    stopRx();
    if(!c5_tx_start(frequency,requested)) {println("ERR TX_RF");startRx();return;}
    apwr=requested;tx=TxStats();txReceived=false;txWatch=0;
    static const uint8_t boundary[Frame]={};writeBytes(boundary,sizeof(boundary));flush();
    flushRx();transmitting=true;txArmedAt=esp_timer_get_time();txLastReady=0;txLastCycles=cycles();
}
void pollTx() {
    const unsigned count=uart_ll_get_rxfifo_len(&UART0);
    for(unsigned n=0;n<count;++n) {uint8_t byte;uart_ll_read_rxfifo(&UART0,&byte,1);txByte(byte);}
    const uint64_t now=esp_timer_get_time();
    if(!txReceived && (!txLastReady || now-txLastReady>=25000)) {
        const uint8_t ready[]={0xef,0xef,0xef,0xef};writeBytes(ready,sizeof(ready));txLastReady=now;
    }
    if(c5_tx_queued_samples()>=StartLevel) {
        flush();c5_tx_run(pumpTx,nullptr);finishTx();return;
    }
    if((!txReceived && now-txArmedAt>=ArmTimeoutMs*1000u) ||
       (txReceived && uint32_t(cycles()-txLastCycles)>=CpuHz/1000u*InputTimeoutMs))finishTx();
}

bool parseFrequency(const char *text,uint64_t &value) {
    if(!text || !*text)return false;
    uint64_t number=0;
    for(const char *p=text;*p;++p) {
        if(*p<'0'||*p>'9')return false;
        const unsigned digit=unsigned(*p-'0');
        if(number>(UINT64_MAX-digit)/10u)return false;
        number=number*10u+digit;
    }
    value=number;return true;
}
bool parseUnsigned(const char *text,uint32_t &value) {
    uint64_t number;
    if(!parseFrequency(text,number)||number>UINT32_MAX)return false;
    value=(uint32_t)number;return true;
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
    line("OK DIAG C5EST adc_bits=10 window=%lu source_hz=%lu iq_hz=%lu cycles_mean=%lu cycles_max=%lu\n",
      (unsigned long)lastRx.iq_window_samples,(unsigned long)lastRx.source_rate_hz,(unsigned long)lastRx.iq_rate_hz,
      (unsigned long)lastRx.mean_cycles_per_iq,(unsigned long)lastRx.max_read_cycles);
    line("OK DIAG RXRF peak_adc=%lu pcm_clipped=%lu unwraps=%lu cfo=%ld dc_i=%ld dc_q=%ld min_hz=%ld max_hz=%ld\n",
      (unsigned long)lastRx.peak,(unsigned long)lastRx.pcm_clipped,(unsigned long)lastRx.fm_unwraps,
      (long)lastRx.frequency_hz,(long)lastRx.dc_i_adc,(long)lastRx.dc_q_adc,
      (long)lastRx.discriminator_min_hz,(long)lastRx.discriminator_max_hz);
    line("OK DIAG RXGAIN updates=%lu index=%lu maximum=%lu\n",
      (unsigned long)lastRx.gain_updates,(unsigned long)lastRx.gain_index,(unsigned long)lastRx.gain_maximum);
    uint16_t rf=0,bb=0,attenuation=0;
    c5_tx_radio_power(&rf,&bb,&attenuation);
    line("OK DIAG TXRF band=%u rf=%u bb=%u nco=%u row=%u lo=%llu error=%s\n",
      c5_tx_radio_is_5g()?5u:2u,(unsigned)rf,(unsigned)bb,(unsigned)attenuation,
      c5_tx_radio_table_index(),(unsigned long long)c5_tx_radio_lo_hz(),
      c5_tx_radio_last_error());
    line("OK DIAG END\n");
}
void command(char *text) {
    char *save=nullptr;const char *name=strtok_r(text," \t",&save);if(!name)return;
    const char *value=strtok_r(nullptr," \t",&save);
    if(!value && !strcmp(name,"STATUS")) {println("OK STATUS CB_FM_STEREO 4");return;}
    if(!value && !strcmp(name,"CAPABILITIES?")) {
        constexpr const char *bands="2401000000-2482500000,5180000000-5885000000";
        line("OK CAPABILITIES RX=%s TX=%s\n",bands,bands);return;
    }
    if(!value && !strcmp(name,"STOP")) {receiveRequested=false;stopRx();c5_radio_idle();println("OK STOP");return;}
    if(!value && !strcmp(name,"DIAG")) {diagnostics();return;}
#if CB13_BENCH_GAIN
    if(!strcmp(name,"GAIN")&&value) {c5_radio_set_rx_gain(atoi(value));println("OK GAIN");return;}
#endif
    if(!strcmp(name,"TX")) {
        uint32_t requested;
        if(!parseUnsigned(value,requested)||requested>191||strtok_r(nullptr," \t",&save)) {println("ERR TX_APWR");return;}
        beginTx(uint8_t(requested));return;
    }
    if(strcmp(name,"RX")||!value) {println("ERR COMMAND");return;}
    uint64_t selected;
    if(!parseFrequency(value,selected)) {println("ERR FREQUENCY");return;}
    if(selected>=1 && selected<=13)selected=(2407u+5u*selected)*UINT64_C(1000000)+312500u;
    if(!c5_rx_frequency_valid(selected)) {println("ERR FREQUENCY");return;}
    const char *mode=strtok_r(nullptr," \t",&save);unsigned chosen=3;
    for(unsigned n=0;n<3;++n)if(mode&&!strcmp(mode,ProfileNames[n]))chosen=n;
    if(chosen==3) {println("ERR MODE");return;}
    const char *offset=strtok_r(nullptr," \t",&save);int32_t selectedFine=0;
    if((offset&&!parseFine(offset,selectedFine))||strtok_r(nullptr," \t",&save)) {println("ERR FINE");return;}
    frequency=selected;profile=chosen;fine=selectedFine;receiveRequested=true;
}
} // namespace

extern "C" void cb_fm_stereo_run() {
    if(!initializeUart() || !c5_tx_init())for(;;)delay(1000);
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
