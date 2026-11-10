#pragma once

// Le générateur FM et le récepteur se partagent l'UART et la radio.
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
