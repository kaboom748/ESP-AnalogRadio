#pragma once

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
  Serial.begin(baud, SERIAL_8N1);
  Serial.setDebugOutput(false); // Ne jamais melanger logs texte et protocole binaire.
  Serial.flush();
  commandIrqMask=rd32(UART_INT_ENA); // Masque installe par le core Arduino.
}
static void uartEnterStream(){
  // Le navigateur attend READY avant d’envoyer les échantillons.
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
