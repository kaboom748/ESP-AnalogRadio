#pragma once

constexpr uint32_t cpuHz = 160000000;
constexpr uint32_t baud = 2000000;
constexpr uint32_t FM_FS = 120000;
constexpr uint32_t RAM_SIZE = 16384;
constexpr uint32_t RING_MASK = RAM_SIZE - 1;
constexpr uint32_t START_LEVEL = 4096;
static_assert((RAM_SIZE & RING_MASK) == 0, "La taille du tampon doit etre une puissance de deux");
static_assert(START_LEVEL < RAM_SIZE, "Seuil de demarrage TX invalide");
constexpr uint32_t UART_TIMEOUT_MS = 150;
constexpr uint32_t TX_ARM_TIMEOUT_MS = 1200;
constexpr uint32_t TX_FAULT_DRAIN_MS = 120;
constexpr uint32_t READY_REPEAT_MS = 25;
constexpr uint8_t cmdPtt = 0xa5;
constexpr uint8_t streamReady = 0xef;
// APWR : 0 = émission maximale, 191 = émission minimale.
constexpr uint8_t maxApwr = 191;
uint8_t currentApwr = 63;
constexpr int32_t deviationHz = 50000;
constexpr int32_t FM_K_OFFSET_STEPS = 4;
constexpr int32_t toneOffsetHz = FM_K_OFFSET_STEPS * 78125;
constexpr uint32_t defaultFrequencyHz = 2442312500UL;
constexpr uint32_t minFrequencyHz = 2401000000UL;
constexpr uint32_t maxFrequencyHz = 2482500000UL;
constexpr int32_t maxRxFineHz = 50000;

