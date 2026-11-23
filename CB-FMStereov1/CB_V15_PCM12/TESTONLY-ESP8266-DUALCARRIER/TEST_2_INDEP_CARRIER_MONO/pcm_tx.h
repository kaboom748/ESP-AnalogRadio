#pragma once

constexpr uint32_t PcmTxSamplesPerFrame = 108;
constexpr uint32_t PcmTxAudioSize = 3 * PcmTxSamplesPerFrame;
// Keep the complete PCMT frame on the wire. The last 30 bytes are RDS
// envelope data in the universal HTML, which this mono transmitter skips.
constexpr uint32_t PcmTxBodySize = 4 + PcmTxAudioSize + 30;
constexpr uint32_t PcmTxQueueFrames = 16;
constexpr uint32_t PcmTxRfPerFrame = PcmTxSamplesPerFrame * (MONO_RF_HZ / PCM12_INPUT_HZ);
constexpr uint32_t PcmTxStartLevel = 12 * PcmTxRfPerFrame;
constexpr uint32_t PcmTxFlowWindow = 18;
constexpr uint32_t PcmTxFlowStep = 4;
constexpr uint32_t PcmTxFlowRefreshMs = 50;
constexpr uint32_t PcmTxProducerCycles = cpuHz / 8000;
static_assert(PcmTxFlowWindow * PcmTxRfPerFrame <= PcmRingSamples, "PCM credit window exceeds the ring");
static_assert(PcmTxStartLevel <= PcmTxQueueFrames * PcmTxRfPerFrame, "Initial credit must cover preroll");

// Symmetric half-sample phase of the original V15 Q14 interpolator.
// Each tap multiplies the sum of a mirrored pair of history samples.
static constexpr int16_t PcmHalfPhase[8]={-149,265,-412,693,-1089,1796,-3287,10375};
static_assert(2*(-149+265-412+693-1089+1796-3287+10375)==16384,
              "Half-sample phase must have unity DC gain");

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

alignas(4) static PcmMonoPair pcmTxHistory[32];
static uint8_t pcmTxHistoryAt;

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
  pcmTxHistoryAt = 0;
}

static void publishPcmTxCredit(uint32_t now) {
  if (now == pcmTxLastCreditPoll) return;
  pcmTxLastCreditPoll = now;
  const uint32_t consumed = tail - pcmTxPlayedSamples;
  if (consumed >= PcmTxRfPerFrame) {
    const uint32_t frames = consumed / PcmTxRfPerFrame;
    pcmTxPlayed += frames;
    pcmTxPlayedSamples += frames * PcmTxRfPerFrame;
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

static inline __attribute__((always_inline)) int16_t clipQ4(int32_t sample) {
  return int16_t(constrain(sample,-PcmQ4FullScale,PcmQ4FullScale));
}

static void IRAM_ATTR __attribute__((optimize("O2"))) renderPcmPair(const uint8_t* frame) {
  const uint8_t* audio=frame+4+3*pcmTxPairOffset;
  const uint32_t started=cyc();
  const PcmMonoPair current={
    extendPcm12((uint16_t(audio[2])<<4)|(audio[1]>>4)),
    extendPcm12(uint16_t(audio[0])|(uint16_t(audio[1]&15)<<8)),
  };
  pcmTxHistoryAt=(pcmTxHistoryAt+1u)&15u;
  pcmTxHistory[pcmTxHistoryAt]=pcmTxHistory[pcmTxHistoryAt+16u]=current;
  const PcmMonoPair* newest=&pcmTxHistory[pcmTxHistoryAt+16u];

  int32_t right=512,left=512;
#pragma GCC unroll 8
  for(int tap=0;tap<8;++tap){
    const PcmMonoPair near=newest[-tap];
    const PcmMonoPair far=newest[tap-15];
    const int32_t coefficient=PcmHalfPhase[tap];
    right+=(int32_t(near.right)+far.right)*coefficient;
    left+=(int32_t(near.left)+far.left)*coefficient;
  }

  const PcmMonoPair delayed=newest[-8];
  const uint32_t position=head;
  audioRing[position&PcmRingMask]={clipQ4(delayed.right*16),clipQ4(delayed.left*16)};
  audioRing[(position+1u)&PcmRingMask]={clipQ4(right>>10),clipQ4(left>>10)};
  memw();
  head=position+2u;
  if(++pcmTxPairOffset==PcmTxSamplesPerFrame){
    pcmTxPairOffset=0;
    ++pcmTxFrameRead;
  }
  const uint32_t elapsed=cyc()-started;
  if(elapsed>pcmTxWorstPairCycles)pcmTxWorstPairCycles=elapsed;
}

static void IRAM_ATTR fillPcmMonoRing() {
  const uint32_t started=cyc();
  while(pcmTxFrameRead!=pcmTxFrameWrite && queuedPairs()<=PcmRingSamples-2u){
    renderPcmPair(pcmTxFrames[pcmTxFrameRead%PcmTxQueueFrames]);
    // Yield to UART polling before a full receive FIFO can accumulate.
    if(cyc()-started>=PcmTxProducerCycles)break;
  }
}
