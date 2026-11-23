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
static_assert(MpxQ4FullScale <= INT16_MAX, "Le multiplex Q4 doit tenir dans le tampon TX");
static_assert(PcmTxFlowWindow * PcmTxMpxPerFrame <= PcmRingSamples, "Fenêtre PCM trop grande");
static_assert(PcmTxFlowWindow * PcmTxMpxPerFrame >= START_LEVEL, "Fenêtre PCM trop petite");

// Interpolation 36 -> 120 kéch/s. Les trois phases non nulles d’une paire
// partagent chaque lecture d’historique. Coefficients Q14 par tap.
constexpr int16_t PcmTxFirTriples[3][48] = {
  {-82, -144, -42, 226, 260, 53, -325, -408, -129, 531, 690, 227, -820, -1076, -376, 1321, 1792, 673, -2323, -3366, -1399, 5974, 12347, 16109, 14025, 8202, 1769, -3094, -2916, -776, 1603, 1626, 466, -957, -1002, -291, 602, 648, 206, -372, -385, -133, 209, 242, 98, -134, -126, -71},
  {-71, -126, -134, 98, 242, 209, -133, -385, -372, 206, 648, 602, -291, -1002, -957, 466, 1626, 1603, -776, -2916, -3094, 1769, 8202, 14025, 16109, 12347, 5974, -1399, -3366, -2323, 673, 1792, 1321, -376, -1076, -820, 227, 690, 531, -129, -408, -325, 53, 260, 226, -42, -144, -82},
  {-214, -149, -80, 113, 265, 153, -163, -412, -250, 364, 693, 455, -546, -1089, -709, 927, 1796, 1236, -1575, -3287, -2438, 3801, 10375, 15310, 15310, 10375, 3801, -2438, -3287, -1575, 1236, 1796, 927, -709, -1089, -546, 455, 693, 364, -250, -412, -163, 153, 265, 113, -80, -149, -214},
};


constexpr uint8_t PcmTxFirTaps = 16;
static constexpr bool pcmFirHasUnityDcGain() {
  for (const auto& row : PcmTxFirTriples) {
    for (uint8_t phase = 0; phase < 3; ++phase) {
      int32_t sum = 0;
      for (uint8_t tap = 0; tap < PcmTxFirTaps; ++tap) sum += row[tap * 3 + phase];
      if (sum != 16384) return false;
    }
  }
  return true;
}
static_assert(pcmFirHasUnityDcGain(), "The constant-input shortcut must match each FIR phase");

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
alignas(4) static PcmStereo pcmTxHistory[32];
static uint8_t pcmTxHistoryAt;
static uint8_t pcmTxIdenticalPairs;
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
  pcmTxIdenticalPairs = 0;
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
  const int32_t mpx = programme + PcmTxPilot[pcmTxPhase] + rds;
  if (++pcmTxPhase == 120) pcmTxPhase = 0;
  return int16_t(constrain(mpx, -MpxQ4FullScale, MpxQ4FullScale));
}

struct PcmFirGroup { int32_t left[3], right[3]; };

// The oldest history word is the base for the load-immediate addressing.
// This separate kernel keeps six sums in registers across all filter taps.
static void IRAM_ATTR __attribute__((noinline, optimize("O2")))
filterPcmTriple(const PcmStereo* oldest, const int16_t* coefficients, PcmFirGroup& output) {
  int32_t left0 = 512, left1 = 512, left2 = 512;
  int32_t right0 = 512, right1 = 512, right2 = 512;
  uint32_t pair, rightSample, coefficient, leftProduct, rightProduct;
#define PCM_TX_FIR_MULTIPLY \
  "mul16s %[leftProduct], %[pair], %[coefficient]\n\t" \
  "mul16s %[rightProduct], %[rightSample], %[coefficient]\n\t"
#define PCM_TX_FIR_COEFF(Offset) \
  "l16ui %[coefficient], %[coefficients], " #Offset "\n\t"
#define PCM_TX_FIR_ACCUMULATE(Left, Right) \
  "add %[" #Left "], %[" #Left "], %[leftProduct]\n\t" \
  "add %[" #Right "], %[" #Right "], %[rightProduct]\n\t"
#define PCM_TX_FIR_TAP(SampleOffset, Coeff0, Coeff1, Coeff2) \
  "l32i %[pair], %[samples], " #SampleOffset "\n\t" \
  PCM_TX_FIR_COEFF(Coeff0) \
  "srai %[rightSample], %[pair], 16\n\t" \
  PCM_TX_FIR_MULTIPLY \
  PCM_TX_FIR_COEFF(Coeff1) \
  PCM_TX_FIR_ACCUMULATE(left0, right0) \
  PCM_TX_FIR_MULTIPLY \
  PCM_TX_FIR_COEFF(Coeff2) \
  PCM_TX_FIR_ACCUMULATE(left1, right1) \
  PCM_TX_FIR_MULTIPLY \
  PCM_TX_FIR_ACCUMULATE(left2, right2)
  asm volatile(
    PCM_TX_FIR_TAP(60, 0, 2, 4)
    PCM_TX_FIR_TAP(56, 6, 8, 10)
    PCM_TX_FIR_TAP(52, 12, 14, 16)
    PCM_TX_FIR_TAP(48, 18, 20, 22)
    PCM_TX_FIR_TAP(44, 24, 26, 28)
    PCM_TX_FIR_TAP(40, 30, 32, 34)
    PCM_TX_FIR_TAP(36, 36, 38, 40)
    PCM_TX_FIR_TAP(32, 42, 44, 46)
    PCM_TX_FIR_TAP(28, 48, 50, 52)
    PCM_TX_FIR_TAP(24, 54, 56, 58)
    PCM_TX_FIR_TAP(20, 60, 62, 64)
    PCM_TX_FIR_TAP(16, 66, 68, 70)
    PCM_TX_FIR_TAP(12, 72, 74, 76)
    PCM_TX_FIR_TAP(8, 78, 80, 82)
    PCM_TX_FIR_TAP(4, 84, 86, 88)
    PCM_TX_FIR_TAP(0, 90, 92, 94)
    : [left0] "+&a" (left0), [left1] "+&a" (left1), [left2] "+&a" (left2),
      [right0] "+&a" (right0), [right1] "+&a" (right1), [right2] "+&a" (right2),
      [pair] "=&a" (pair), [rightSample] "=&a" (rightSample), [coefficient] "=&a" (coefficient),
      [leftProduct] "=&a" (leftProduct), [rightProduct] "=&a" (rightProduct)
    : [samples] "a" (oldest), [coefficients] "a" (coefficients)
    : "memory"
  );
#undef PCM_TX_FIR_TAP
#undef PCM_TX_FIR_ACCUMULATE
#undef PCM_TX_FIR_COEFF
#undef PCM_TX_FIR_MULTIPLY
  output = {{left0 >> 10, left1 >> 10, left2 >> 10}, {right0 >> 10, right1 >> 10, right2 >> 10}};
}

static void IRAM_ATTR __attribute__((optimize("O2"))) renderPcmPair(const uint8_t* frame) {
  const uint8_t* audio = frame + 4 + 3 * pcmTxPairOffset;
  const uint32_t started = cyc();
  const PcmStereo previous = pcmTxHistory[pcmTxHistoryAt];
  pcmTxHistoryAt = (pcmTxHistoryAt + 1) & 15;
  const PcmStereo current = {
    extendPcm12(uint16_t(audio[0]) | (uint16_t(audio[1] & 15) << 8)),
    extendPcm12((uint16_t(audio[2]) << 4) | (audio[1] >> 4)),
  };
  if (current.left == previous.left && current.right == previous.right) {
    if (pcmTxIdenticalPairs < PcmTxFirTaps) ++pcmTxIdenticalPairs;
  } else {
    pcmTxIdenticalPairs = 1;
  }
  pcmTxHistory[pcmTxHistoryAt] = pcmTxHistory[pcmTxHistoryAt + 16] = current;
  const uint8_t* envelope = frame + 4 + PcmTxAudioSize;
  const uint8_t phase = pcmTxInterpolationPhase;
  const PcmStereo* history = &pcmTxHistory[pcmTxHistoryAt + 16];
  PcmFirGroup filtered;
  // Constant input gives the same Q4 value for every normalized phase.
  if (pcmTxIdenticalPairs == PcmTxFirTaps) {
    const int32_t left = current.left * 16;
    const int32_t right = current.right * 16;
    filtered = {{left, left, left}, {right, right, right}};
  } else {
    filterPcmTriple(history - 15, PcmTxFirTriples[phase], filtered);
  }
  uint32_t position = head;
  if (phase == 0) {
    const PcmStereo delayed = history[-8];
    audioRing.mpxQ4[position++ & PcmRingMask] = renderPcmMpx(delayed.left * 16, delayed.right * 16, envelope);
  }
  // Input phases recur in the order 0, 2, 1. Within each pair, write the
  // pure-delay output first when present, then the three filtered phases.
  for (uint8_t index = 0; index < 3; ++index) {
    audioRing.mpxQ4[position++ & PcmRingMask] = renderPcmMpx(filtered.left[index], filtered.right[index], envelope);
  }
  pcmTxInterpolationPhase = phase == 0 ? 2 : phase - 1;
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
