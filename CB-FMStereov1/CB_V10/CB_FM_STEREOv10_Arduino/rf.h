#pragma once

static constexpr uint16_t baseK=FM_K_OFFSET_STEPS;
static constexpr int32_t carrierQ16=FM_K_OFFSET_STEPS*65536L;
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
static bool releaseTxRf(){
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
  return ok;
}
static void toneProgramOff(){
  uint32_t r=(rd32(TONE1)&~(K_MASK|SCALE_MASK|GATE_MASK))|(uint32_t(baseK)&K_MASK);
  wr32(TONE1,r);wr32(TONE2,rd32(TONE2)&~GATE_MASK);wr32(TONE3,rd32(TONE3)&~GATE_MASK);memw();
}
