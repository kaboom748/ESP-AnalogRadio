#pragma once

constexpr uint32_t cpuHz = 160000000;
constexpr uint32_t baud = 1500000;
constexpr uint32_t PCM12_INPUT_HZ = 36000;
constexpr uint32_t MONO_RF_HZ = 2 * PCM12_INPUT_HZ;
constexpr uint32_t RAM_SIZE = 16384;
constexpr uint32_t UART_TIMEOUT_MS = 150;
constexpr uint32_t TX_ARM_TIMEOUT_MS = 1200;
constexpr uint32_t TX_FAULT_DRAIN_MS = 120;
constexpr uint32_t READY_REPEAT_MS = 25;
constexpr uint8_t streamReady = 0xef;
// APWR : 0 = émission maximale, 191 = émission minimale.
constexpr uint8_t maxApwr = 191;
uint8_t currentApwr = 63;
constexpr int32_t deviationHz = 100000;
constexpr int32_t NcoStepHz = 78125;
constexpr int32_t FM_K_OFFSET_STEPS = 4;
constexpr int32_t toneOffsetHz = FM_K_OFFSET_STEPS * NcoStepHz;
constexpr uint32_t secondCarrierOffsetHz = 1562500u;
constexpr uint32_t defaultFrequencyHz = 2442312500UL;
constexpr uint32_t minFrequencyHz = 2401000000UL;
constexpr uint32_t maxFrequencyHz = 2482500000UL;
// Laisser la place à la deuxième porteuse et à l'arrondi du PLL (~977 Hz).
constexpr uint32_t maxTxFrequencyHz = maxFrequencyHz - secondCarrierOffsetHz - 500u;
constexpr int32_t maxRxFineHz = 50000;

