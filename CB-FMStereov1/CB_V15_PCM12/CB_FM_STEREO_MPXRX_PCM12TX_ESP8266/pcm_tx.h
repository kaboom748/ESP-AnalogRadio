#pragma once

constexpr uint32_t PcmTxSamplesPerFrame = 108;
constexpr uint32_t PcmTxAudioSize = 3 * PcmTxSamplesPerFrame;
constexpr uint32_t PcmTxRdsSize = 30;
constexpr uint32_t PcmTxBodySize = 4 + PcmTxAudioSize + PcmTxRdsSize;
constexpr uint32_t PcmTxQueueFrames = 16;
constexpr uint32_t PcmTxMpxPerFrame = PcmTxSamplesPerFrame * 10 / 3;
constexpr uint32_t PcmTxFlowWindow = 20;
constexpr uint32_t PcmTxFlowStep = 4;
constexpr uint32_t PcmTxFlowRefreshMs = 50;
constexpr uint32_t PcmTxProducerCycles = cpuHz / 8000;
constexpr int32_t PcmTxFullScale = 2047;
constexpr int32_t PcmTxMaxArea = int32_t((int64_t(deviationHz) * PcmToneStep + 39062) / 78125);
constexpr int32_t PcmTxSlope = int32_t(
  (int64_t(PcmTxMaxArea) * 16384 + PcmTxFullScale / 2) / PcmTxFullScale);
static_assert(PcmTxMaxArea < 32768, "Le tampon TX doit contenir les aires en Q5");
static_assert(PcmTxFlowWindow * PcmTxMpxPerFrame <= PcmRingSamples, "Fenêtre PCM trop grande");
static_assert(PcmTxFlowWindow * PcmTxMpxPerFrame >= START_LEVEL, "Fenêtre PCM trop petite");

// Interpolation 36 -> 120 kéch/s (rapport 10/3), FIR polyphasé Q14.
// Le filtre de 101 coefficients garde la bande utile jusqu'à 14,2 kHz.
// La phase 0 est un retard pur et ne passe pas par la boucle du filtre.
constexpr int16_t PcmTxFir[10][10] = {
  {0, 0, 0, 0, 0, 16384, 0, 0, 0, 0},
  {68, -169, 350, -698, 1706, 15995, -1368, 605, -305, 144},
  {143, -348, 712, -1429, 3683, 15189, -2356, 1073, -540, 252},
  {219, -518, 1049, -2122, 5838, 13902, -2955, 1378, -693, 318},
  {284, -656, 1320, -2697, 8060, 12214, -3181, 1512, -759, 343},
  {329, -742, 1485, -3075, 10227, 10227, -3075, 1485, -742, 329},
  {343, -759, 1512, -3181, 12214, 8060, -2697, 1320, -656, 284},
  {318, -693, 1378, -2955, 13902, 5838, -2122, 1049, -518, 219},
  {252, -540, 1073, -2356, 15189, 3683, -1429, 712, -348, 143},
  {144, -305, 605, -1368, 15995, 1706, -698, 350, -169, 68},
};

// Le pilote et les sous-porteuses utilisent une seule origine de phase.
constexpr int16_t PcmTxPilot[120] = {
  0, 2472, 2693, 461, -2191, -2847, -911, 1855, 2932, 1338, -1474, -2944,
  -1733, 1056, 2883, 2084, -613, -2752, -2385, 154, 2553, 2626, 308, -2291,
  -2803, -763, 1972, 2911, 1199, -1605, -2948, -1605, 1199, 2911, 1972, -763,
  -2803, -2291, 308, 2626, 2553, 154, -2385, -2752, -613, 2084, 2883, 1056,
  -1733, -2944, -1474, 1338, 2932, 1855, -911, -2847, -2191, 461, 2693, 2472,
  0, -2472, -2693, -461, 2191, 2847, 911, -1855, -2932, -1338, 1474, 2944,
  1733, -1056, -2883, -2084, 613, 2752, 2385, -154, -2553, -2626, -308, 2291,
  2803, 763, -1972, -2911, -1199, 1605, 2948, 1605, -1199, -2911, -1972, 763,
  2803, 2291, -308, -2626, -2553, -154, 2385, 2752, 613, -2084, -2883, -1056,
  1733, 2944, 1474, -1338, -2932, -1855, 911, 2847, 2191, -461, -2693, -2472
};
constexpr int16_t PcmTxDifference[120] = {
  0, 17138, -13941, -5797, 18657, -9380, -11027, 18350, -3900, -15177, 16246, 1961,
  -17842, 12553, 7630, -18760, 7630, 12553, -17842, 1961, 16246, -15177, -3900, 18350,
  -11027, -9380, 18657, -5797, -13941, 17138, 0, -17138, 13941, 5797, -18657, 9380,
  11027, -18350, 3900, 15177, -16246, -1961, 17842, -12553, -7630, 18760, -7630, -12553,
  17842, -1961, -16246, 15177, 3900, -18350, 11027, 9380, -18657, 5797, 13941, -17138,
  0, 17138, -13941, -5797, 18657, -9380, -11027, 18350, -3900, -15177, 16246, 1961,
  -17842, 12553, 7630, -18760, 7630, 12553, -17842, 1961, 16246, -15177, -3900, 18350,
  -11027, -9380, 18657, -5797, -13941, 17138, 0, -17138, 13941, 5797, -18657, 9380,
  11027, -18350, 3900, 15177, -16246, -1961, 17842, -12553, -7630, 18760, -7630, -12553,
  17842, -1961, -16246, 15177, 3900, -18350, 11027, 9380, -18657, 5797, 13941, -17138
};
// Trois tours stockés pour indexer le RDS sans division logicielle.
constexpr int16_t PcmTxRdsCarrier[120] = {
  330, -326, 314, -294, 267, -233, 194, -150, 102, -52, 0, 52,
  -102, 150, -194, 233, -267, 294, -314, 326, -330, 326, -314, 294,
  -267, 233, -194, 150, -102, 52, 0, -52, 102, -150, 194, -233,
  267, -294, 314, -326, 330, -326, 314, -294, 267, -233, 194, -150,
  102, -52, 0, 52, -102, 150, -194, 233, -267, 294, -314, 326,
  -330, 326, -314, 294, -267, 233, -194, 150, -102, 52, 0, -52,
  102, -150, 194, -233, 267, -294, 314, -326, 330, -326, 314, -294,
  267, -233, 194, -150, 102, -52, 0, 52, -102, 150, -194, 233,
  -267, 294, -314, 326, -330, 326, -314, 294, -267, 233, -194, 150,
  -102, 52, 0, -52, 102, -150, 194, -233, 267, -294, 314, -326
};

static uint8_t pcmTxFrames[PcmTxQueueFrames][PcmTxBodySize];
static uint32_t pcmTxFrameWrite;
static uint32_t pcmTxFrameRead;
static uint32_t pcmTxExpectedIndex;
static uint32_t pcmTxReceivedIndex;
static uint32_t pcmTxPeakFrames;
static uint16_t pcmTxBodyOffset;
static uint8_t pcmTxMagicOffset;
static bool pcmTxSequenceStarted;
static const char* pcmTxProtocolError;
static uint32_t pcmTxFifoErrors;
static uint32_t pcmTxLastUartError;
static uint32_t pcmTxWorstPairCycles;
static uint16_t pcmTxPairOffset;
static bool pcmTxFlow;
static bool pcmTxFlowReady;
static uint32_t pcmTxGranted;
static uint32_t pcmTxGrantAt;
static uint32_t pcmTxLastCreditPoll;
static uint32_t pcmTxPlayed;
static uint32_t pcmTxPlayedSamples;

struct PcmStereo { int16_t left, right; };
static PcmStereo pcmTxHistory[32];
static uint8_t pcmTxHistoryAt;
static uint8_t pcmTxInterpolationPhase;
static uint8_t pcmTxPhase;
static uint8_t pcmTxRdsPhase;
static uint8_t pcmTxRdsOffset;
static int16_t pcmTxRdsCurrent;
static int16_t pcmTxRdsStep;
static int16_t pcmTxRdsInterpolated;

static void resetPcmTx(bool flow) {
  pcmTxFrameRead = pcmTxFrameWrite = pcmTxExpectedIndex = pcmTxReceivedIndex = pcmTxPeakFrames = 0;
  pcmTxBodyOffset = pcmTxMagicOffset = pcmTxPairOffset = 0;
  pcmTxSequenceStarted = false;
  pcmTxProtocolError = nullptr;
  pcmTxFifoErrors = pcmTxWorstPairCycles = 0;
  pcmTxLastUartError = 0;
  pcmTxFlow = flow;
  pcmTxFlowReady = false;
  pcmTxGranted = pcmTxGrantAt = pcmTxPlayed = pcmTxPlayedSamples = 0;
  pcmTxLastCreditPoll = UINT32_MAX;
  txPcmMissed = txPcmUnderruns = txPcmTailUnderruns = txPcmMinQueued = 0;
  memset(pcmTxHistory, 0, sizeof(pcmTxHistory));
  pcmTxHistoryAt = pcmTxInterpolationPhase = pcmTxPhase = pcmTxRdsPhase = pcmTxRdsOffset = 0;
  pcmTxRdsCurrent = pcmTxRdsStep = pcmTxRdsInterpolated = 0;
}

static void publishPcmTxCredit(uint32_t now) {
  if (now == pcmTxLastCreditPoll) return;
  pcmTxLastCreditPoll = now;
  const uint32_t consumed = tail - pcmTxPlayedSamples;
  if (consumed >= PcmTxMpxPerFrame) {
    const uint32_t frames = consumed / PcmTxMpxPerFrame;
    pcmTxPlayed += frames;
    pcmTxPlayedSamples += frames * PcmTxMpxPerFrame;
  }
  // Ingress capacity only increases after the DSP releases a full frame.
  // RF consumption bounds the total number of queued and in-flight frames.
  const uint32_t window = min(PcmTxFlowWindow, pcmTxFrameRead - pcmTxPlayed + PcmTxQueueFrames);
  const uint32_t limit = pcmTxPlayed + window;
  if (limit - pcmTxGranted < PcmTxFlowStep && now - pcmTxGrantAt < PcmTxFlowRefreshMs) return;

  static constexpr char prefix[] = "OK PCM CREDIT ";
  char digits[10];
  // Keep the complete line in the FIFO without waiting on USB or using Serial.
  if (txCount() > 127 - (sizeof(prefix) - 1 + sizeof(digits) + 1)) return;
  uint32_t value = limit;
  char* digit = digits + sizeof(digits);
  do {
    *--digit = '0' + value % 10;
    value /= 10;
  } while (value);
  for (const char* c = prefix; *c; ++c) uartSend(*c);
  for (; digit != digits + sizeof(digits); ++digit) uartSend(*digit);
  uartSend('\n');
  pcmTxGranted = limit;
  pcmTxGrantAt = now;
}

static void acceptPcmTxByte(uint8_t byte) {
  static constexpr uint8_t magic[] = {'P', 'C', 'M', 'T'};
  if (pcmTxProtocolError) return;
  if (pcmTxMagicOffset < sizeof(magic)) {
    if (byte == magic[pcmTxMagicOffset]) {
      ++pcmTxMagicOffset;
      if (pcmTxMagicOffset == sizeof(magic)) {
        if (pcmTxFlow && pcmTxFrameWrite == pcmTxGranted) {
          pcmTxProtocolError = "TX_PCM_CREDIT";
        } else if (pcmTxFrameWrite - pcmTxFrameRead >= PcmTxQueueFrames) {
          pcmTxProtocolError = "TX_PCM_QUEUE";
        }
      }
    } else {
      pcmTxMagicOffset = byte == magic[0] ? 1 : 0;
      if (pcmTxSequenceStarted) pcmTxProtocolError = "TX_PCM_MAGIC";
    }
    return;
  }
  auto& frame = pcmTxFrames[pcmTxFrameWrite % PcmTxQueueFrames];
  frame[pcmTxBodyOffset++] = byte;
  if (pcmTxBodyOffset != PcmTxBodySize) return;
  pcmTxMagicOffset = pcmTxBodyOffset = 0;
  const uint32_t first = uint32_t(frame[0]) | (uint32_t(frame[1]) << 8) |
                         (uint32_t(frame[2]) << 16) | (uint32_t(frame[3]) << 24);
  pcmTxReceivedIndex = first;
  if (pcmTxSequenceStarted && first != pcmTxExpectedIndex) {
    pcmTxProtocolError = "TX_PCM_SEQUENCE";
    return;
  }
  pcmTxSequenceStarted = true;
  pcmTxExpectedIndex = first + PcmTxSamplesPerFrame;
  const uint32_t queued = ++pcmTxFrameWrite - pcmTxFrameRead;
  if (queued > pcmTxPeakFrames) pcmTxPeakFrames = queued;
}

static inline int16_t extendPcm12(uint16_t value) {
  return int16_t(int32_t(value ^ 0x800u) - 0x800);
}

static inline __attribute__((always_inline)) int16_t renderPcmMpx(int32_t leftQ4, int32_t rightQ4, const uint8_t* envelope) {
  const int32_t difference = (((leftQ4 - rightQ4) >> 1) * PcmTxDifference[pcmTxPhase] + 4096) >> 13;
  const int32_t programme = (leftQ4 + rightQ4 + difference) >> 1;
  if (pcmTxRdsPhase == 0) {
    const int16_t previous = pcmTxRdsCurrent;
    pcmTxRdsCurrent = int8_t(envelope[pcmTxRdsOffset]);
    pcmTxRdsStep = pcmTxRdsCurrent - previous;
    pcmTxRdsInterpolated = previous * 12;
    if (++pcmTxRdsOffset == PcmTxRdsSize) pcmTxRdsOffset = 0;
  }
  const int32_t rds = (pcmTxRdsInterpolated * PcmTxRdsCarrier[pcmTxPhase] + 128) >> 8;
  pcmTxRdsInterpolated += pcmTxRdsStep;
  if (++pcmTxRdsPhase == 12) pcmTxRdsPhase = 0;
  const int32_t mpx = (programme + PcmTxPilot[pcmTxPhase] + rds + 8) >> 4;
  if (++pcmTxPhase == 120) pcmTxPhase = 0;
  const int32_t limited = constrain(mpx, -PcmTxFullScale, PcmTxFullScale);
  return int16_t((limited * PcmTxSlope + 8192) >> 14);
}

static inline __attribute__((always_inline)) int16_t interpolatePcmMpx(uint8_t phase, const uint8_t* envelope) {
  const PcmStereo* history = &pcmTxHistory[pcmTxHistoryAt + 16];
  if (phase == 0) {
    const PcmStereo delayed = history[-5];
    return renderPcmMpx(delayed.left * 16, delayed.right * 16, envelope);
  }
  const int16_t* coefficients = PcmTxFir[phase];
  int32_t left = 512, right = 512;
#pragma GCC unroll 10
  for (uint8_t tap = 0; tap < 10; ++tap) {
    const PcmStereo pair = *history--;
    const int16_t coefficient = coefficients[tap];
    left += pair.left * coefficient;
    right += pair.right * coefficient;
  }
  return renderPcmMpx(left >> 10, right >> 10, envelope);
}

static void IRAM_ATTR __attribute__((optimize("O2"))) renderPcmPair(const uint8_t* frame) {
  const uint8_t* audio = frame + 4 + 3 * pcmTxPairOffset;
  const uint32_t started = cyc();
  pcmTxHistoryAt = (pcmTxHistoryAt + 1) & 15;
  const PcmStereo current = {
    extendPcm12(uint16_t(audio[0]) | (uint16_t(audio[1] & 15) << 8)),
    extendPcm12((uint16_t(audio[2]) << 4) | (audio[1] >> 4)),
  };
  pcmTxHistory[pcmTxHistoryAt] = pcmTxHistory[pcmTxHistoryAt + 16] = current;
  const uint8_t* envelope = frame + 4 + PcmTxAudioSize;
  uint8_t phase = pcmTxInterpolationPhase;
  uint32_t position = head;
  do {
    audioRing.pcmArea[position++ & PcmRingMask] = interpolatePcmMpx(phase, envelope);
    phase += 3;
  } while (phase < 10);
  pcmTxInterpolationPhase = phase - 10;
  memw();
  head = position;
  if (++pcmTxPairOffset == PcmTxSamplesPerFrame) {
    pcmTxPairOffset = 0;
    ++pcmTxFrameRead;
  }
  const uint32_t elapsed = cyc() - started;
  if (elapsed > pcmTxWorstPairCycles) pcmTxWorstPairCycles = elapsed;
}

static void IRAM_ATTR fillPcmMpxRing() {
  const uint32_t started = cyc();
  while (pcmTxFrameRead != pcmTxFrameWrite) {
    if (occupancyBytes() > PcmRingSamples - 4) break;
    renderPcmPair(pcmTxFrames[pcmTxFrameRead % PcmTxQueueFrames]);
    // Rendre la main bien avant les 850 µs nécessaires au remplissage de l'UART.
    if (cyc() - started >= PcmTxProducerCycles) break;
  }
}
