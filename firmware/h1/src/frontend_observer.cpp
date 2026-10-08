// SPDX-License-Identifier: Apache-2.0
#include "frontend_observer.hpp"
#include "build_identity.h"
#include <cstring>
#include <limits>
#include <zephyr/kernel.h>

extern "C" {
H1FStorage h1FrontendObserverStorage __attribute__((section(".h1_frontend_observer"),aligned(32)));
}
namespace {
H1FControl &control() { return h1FrontendObserverStorage.header.control; }
constexpr uint32_t ix(H1FCounter c) { return uint32_t(c); }
H1FRecord *active() { return control().active; }
void error(H1FError e) {
 if (active() && !active()->metadata.error) active()->metadata.error=uint32_t(e);
}
void account(uint32_t bytes) {
 auto *r=active(); if (!r) return;
 auto &n=r->metadata.counters[ix(H1FCounter::observer_bytes_written)];
 if (UINT32_MAX-n<bytes) { error(H1FError::CounterOverflow); return; }
 n+=bytes;
}
void add(H1FCounter c,uint32_t value) {
 auto *r=active(); if (!r || !value) return;
 if (ix(c)>=48) { error(H1FError::Bounds); return; }
 auto &n=r->metadata.counters[ix(c)];
 if (UINT32_MAX-n<value) {
  error(H1FError::CounterOverflow);
  auto &overflow=r->metadata.counters[ix(H1FCounter::counter_overflow)];
  if (overflow!=UINT32_MAX) ++overflow;
  return;
 }
 n+=value; account(8); // logical counter write + accounting write
}
uint64_t *slot(H1FRecord &r,uint32_t index) {
 if (index<64) return &r.fixed[index];
 if (index<2320) { index-=64; return &r.frames[index/12][index%12]; }
 if (index<3072) { index-=2320; return &r.groups[index/16][index%16]; }
 return nullptr;
}
void put(uint32_t index,uint64_t value) {
 auto *r=active(); if (!r) return;
 uint64_t *destination=slot(*r,index);
 if (!destination) { add(H1FCounter::record_overflow,1);error(H1FError::Bounds);return; }
 const uint8_t mask=uint8_t(1u<<(index&7));
 uint8_t &bits=r->presence[index/8];
 if (bits&mask) { add(H1FCounter::duplicate_marker_count,1);error(H1FError::DuplicateMarker);return; }
 *destination=value;bits|=mask;
 add(H1FCounter::timestamp_store_count,1);account(13); // timestamp8, bitmap1, accounting4
}
void pair(uint32_t index,uint64_t a,uint64_t b,uint32_t reads) {
 put(index,a);put(index+1,b);add(H1FCounter::clock_read_count,reads);
}
constexpr uint8_t guard(uint32_t i) { return uint8_t(0xb5u^(i*29u)); }
constexpr uint32_t expected[38]={1,47,188,188,188,188,188,188,0,0,47,47,0,188,24064,0,0,24064,23500,23500,62944,188832,1,188832,1,755328,0,0,0,0,11560,11560,3,100,1,1,1,1};
}
bool h1FInit() {
 std::memset(&h1FrontendObserverStorage,0,sizeof(h1FrontendObserverStorage));
 auto &c=control();c.magic=H1_F_MAGIC;c.version=H1_F_VERSION;c.clockHz=sys_clock_hw_cycles_per_sec();c.nextCampaignId=1;
 std::memcpy(c.sourceBundle,H1_SOURCE_BUNDLE_SHA256,65);
 std::memcpy(c.planSha256,"623e53d0d8174ac34b0f4e54093f51ef3af980c77a49f07b806e07eb4e0b941c",65);
 for (uint32_t i=0;i<32;++i) { h1FrontendObserverStorage.prefixGuard[i]=guard(i);h1FrontendObserverStorage.suffixGuard[i]=guard(i); }
 return c.clockHz==H1_F_CLOCK_HZ && h1FGuards();
}
bool h1FGuards() {
 for (uint32_t i=0;i<32;++i)
  if (h1FrontendObserverStorage.prefixGuard[i]!=guard(i) || h1FrontendObserverStorage.suffixGuard[i]!=guard(i)) return false;
 return control().magic==H1_F_MAGIC && control().version==H1_F_VERSION;
}
bool h1FWriterAllowed() { return !control().held || (control().running && control().active); }
bool h1FActive() { return active()!=nullptr; }
uint64_t h1FRead() { return active()?k_cycle_get_64():0; }
void h1FFixedAt(H1FFixed m,uint64_t t,uint32_t reads) { put(uint32_t(m),t);add(H1FCounter::clock_read_count,reads); }
void h1FFixedPair(H1FFixed a,H1FFixed b,uint64_t x,uint64_t y,uint32_t reads) {
 put(uint32_t(a),x);put(uint32_t(b),y);add(H1FCounter::clock_read_count,reads);
}
void h1FFramePair(uint32_t f,H1FFrame field,uint64_t a,uint64_t b,H1FCounter operation,uint32_t reads) {
 if (!active()) return;
 if (f>=188 || uint32_t(field)+1>=12) { error(H1FError::Bounds);return; }
 pair(64+12*f+uint32_t(field),a,b,reads);add(operation,1);
}
void h1FCallPair(uint32_t f,uint64_t a,uint64_t b,bool completed) {
 if (!active()) return;
 if (f>=188) { error(H1FError::Bounds);return; }
 pair(64+12*f,a,b,2);if (completed) add(H1FCounter::SPECTRAL_FRAMES,1);
}
void h1FGroupPair(uint32_t g,H1FGroup field,uint64_t a,uint64_t b) {
 if (!active()) return;
 if (g>=47 || uint32_t(field)+1>=16) { error(H1FError::Bounds);return; }
 pair(2320+16*g+uint32_t(field),a,b,2);
}
void h1FCount(H1FCounter c,uint32_t n) { add(c,n); }
void h1FBackend(bool cmsis,bool capture,uint32_t weights) {
 if (!active()) return;
 auto &m=active()->metadata;m.powerStage=cmsis?8:7;m.weightCount=weights;account(12);
 if (cmsis || capture || weights!=1885) { add(H1FCounter::unexpected_backend_count,1);error(H1FError::Backend); }
}
void h1FMelCompleted(bool mve) {
 add(H1FCounter::MEL_GROUPS,1);add(mve?H1FCounter::MEL_MVE_GROUPS:H1FCounter::MEL_SCALAR_GROUPS,1);
 if (active() && !mve) { add(H1FCounter::unexpected_backend_count,1);error(H1FError::Backend); }
}
void h1FNativeResult(uint64_t total,uint32_t finite,uint32_t crc) {
 if (!active()) return;
 active()->metadata.accounting[0]=total;active()->metadata.finiteCount=finite;active()->metadata.outputCrc32=crc;account(20);
}
H1FError h1FBeginSet(H1FKind kind,uint64_t n0,uint64_t n1,uint64_t epoch,uint64_t inputEpoch,uint64_t generation) {
 auto &c=control();
 if (!h1FGuards()) return H1FError::Guard;
 if (c.held || c.running) return H1FError::Held;
 if (kind==H1FKind::Campaign && c.campaignIssued) return H1FError::AlreadyRun;
 if (!c.nextCampaignId || c.nextCampaignId==UINT64_MAX) return H1FError::Exhausted;
 if (kind!=H1FKind::IsolatedProduction && kind!=H1FKind::Campaign) return H1FError::Bounds;
 c.campaignId=c.nextCampaignId++;c.nonce0=n0;c.nonce1=n1;c.ownerEpoch=epoch;c.inputEpoch=inputEpoch;c.inputGeneration=generation;
 c.kind=uint32_t(kind);c.requested=kind==H1FKind::Campaign?25:1;c.stored=c.warmups=c.measured=0;c.collectOrdinal=c.collectOffset=0;
 c.error=0;c.held=1;c.running=1;c.active=nullptr;c.calibrationCount=c.calibrationStores=c.calibrationBytes=0;
 if (kind==H1FKind::Campaign) c.campaignIssued=1;
 // Entire set initialization is before every primary interval and all warmups.
 std::memset(h1FrontendObserverStorage.records,0,c.requested*sizeof(H1FRecord));
 for (uint32_t i=0;i<c.requested;++i) {
  auto &m=h1FrontendObserverStorage.records[i].metadata;
  m.magic=H1_F_MAGIC;m.version=1;m.ordinal=i;m.kind=c.kind;m.clockHz=sys_clock_hw_cycles_per_sec();m.payloadBytes=H1_F_PAYLOAD_BYTES;
  m.campaignId=c.campaignId;m.nonce0=n0;m.nonce1=n1;m.ownerEpoch=epoch;m.inputEpoch=inputEpoch;m.inputGeneration=generation;
 }
 return H1FError::None;
}
bool h1FBeginRecord(uint32_t ordinal) {
 auto &c=control();
 if (!c.running || c.active || ordinal!=c.stored || ordinal>=c.requested) { c.error=uint32_t(H1FError::Bounds);return false; }
 c.active=&h1FrontendObserverStorage.records[ordinal];c.active->metadata.state=uint32_t(H1FState::Capturing);return true;
}
void h1FStopRecord(H1FError e) {
 auto &c=control();if (!c.active)return;
 if (!c.active->metadata.error)c.active->metadata.error=uint32_t(e);
 c.active->metadata.state=uint32_t(H1FState::Failed);
 if (!c.error) c.error=c.active->metadata.error;
 ++c.stored;c.active=nullptr;
}
void h1FFinishRecord(const H1ProductionProof &p,const H1RuntimeProfile &profile,uint32_t selected,uint32_t productTopCount,uint32_t sequence,uint32_t faults,
 uint32_t lease,uint32_t crc,uint32_t sha,uint32_t identities) {
 auto *r=active();if (!r) return;auto &c=control();auto &m=r->metadata;
 h1FFixedAt(H1FFixed::P0,p.primaryStart);h1FFixedAt(H1FFixed::P1,p.ready);
 h1FFixedAt(H1FFixed::frontendStart,p.frontendStart);h1FFixedAt(H1FFixed::frontendEnd,p.frontendEnd);
 h1FFixedAt(H1FFixed::backboneStart,p.backboneStart);h1FFixedAt(H1FFixed::backboneEnd,p.backboneEnd);
 h1FFixedAt(H1FFixed::gemStart,p.gemStart);h1FFixedAt(H1FFixed::gemEnd,p.gemEnd);
 h1FFixedAt(H1FFixed::classifierStart,p.classifierStart);h1FFixedAt(H1FFixed::classifierEnd,p.classifierEnd);
 h1FFixedAt(H1FFixed::scoreStart,p.scoreStart);h1FFixedAt(H1FFixed::scoreEnd,p.scoreEnd);
 h1FFixedAt(H1FFixed::topStart,p.topStart);h1FFixedAt(H1FFixed::topEnd,p.topEnd);
 h1FFixedAt(H1FFixed::publicationStart,p.publicationStart);h1FFixedAt(H1FFixed::publicationEnd,p.publicationEnd);
 m.inferenceSequence=p.generation.inferenceSequence;m.runSequence=sequence;m.resultStatus=p.status;m.inputProducer=p.input.producer;
 add(H1FCounter::RESULT_EVIDENCE_SCANS,p.operations.resultEvidenceScans);add(H1FCounter::SCORE_EVIDENCE_SCANS,p.operations.scoreEvidenceScans);
 add(H1FCounter::DIAGNOSTIC_REPEAT_COMPARISONS,p.operations.repeatComparisons);add(H1FCounter::DIAGNOSTIC_SAVES,p.operations.diagnosticSaves);
 add(H1FCounter::GENERATED_SCORES,p.operations.generatedScores);add(H1FCounter::SCORE_FINITE_CHECKS,p.operations.finiteChecks);
 add(H1FCounter::PRODUCTION_TOP_COUNT,productTopCount);add(H1FCounter::ACCEPTED_SELECTOR_COUNT,selected);
 add(H1FCounter::U85_BACKBONE_COMMANDS,profile.backbone.command_count);add(H1FCounter::U85_CLASSIFIER_COMMANDS,profile.classifier.command_count);
 add(H1FCounter::U85_BACKBONE_COMPLETIONS,profile.backbone.irq_count);add(H1FCounter::U85_CLASSIFIER_COMPLETIONS,profile.classifier.irq_count);
 m.frameCount=m.counters[ix(H1FCounter::SPECTRAL_FRAMES)];m.groupCount=m.counters[ix(H1FCounter::MEL_GROUPS)];
 m.accounting[1]=crc;m.accounting[2]=sha;m.accounting[3]=identities;m.accounting[4]=lease;
 m.accounting[5]=profile.backbone.lifecycle.copy_count+profile.classifier.lifecycle.copy_count;
 m.accounting[6]=profile.backbone.lifecycle.runtime_init_count+profile.classifier.lifecycle.runtime_init_count;
 m.accounting[7]=profile.backbone.lifecycle.allocate_tensors_count+profile.classifier.lifecycle.allocate_tensors_count;
 m.accounting[8]=p.operations.frontendInternalScans;m.accounting[9]=p.operations.frontendInternalBytes;
 account(108); // metadata32 + nine accounting u64 fields72 + accounting write4
 if (faults) { add(H1FCounter::fault_count,1);error(H1FError::Runtime); }
 if (!h1FGuards()) { add(H1FCounter::guard_failure_count,1);error(H1FError::Guard); }
 if (p.clockHz!=H1_F_CLOCK_HZ || m.clockHz!=H1_F_CLOCK_HZ) error(H1FError::Clock);
 if (!p.admitted || p.status!=1 || !p.ready || p.ready<=p.primaryStart || !lease || crc || sha || identities ||
     p.input.waveformOwnerEpoch!=m.inputEpoch || p.input.waveformGeneration!=m.inputGeneration || p.generation.epoch!=m.ownerEpoch || p.input.producer!=1 ||
     m.accounting[5] || m.accounting[6] || m.accounting[7] || p.operations.frontendInternalScans!=m.counters[ix(H1FCounter::FRONTEND_INTERNAL_CRC_CALLS)] ||
     p.operations.frontendInternalBytes!=m.counters[ix(H1FCounter::FRONTEND_INTERNAL_CRC_BYTES)]) error(H1FError::IncompleteProduction);
 for (uint32_t i=0;i<38;++i) if (m.counters[i]!=expected[i]) error(H1FError::Counts);
 for (uint32_t i=0;i<384;++i) {
  const uint8_t needed=i<6?0xff:i==6?1:i==7?0:0xff;
  const uint8_t missing=needed&uint8_t(~r->presence[i]);
  if (missing) { add(H1FCounter::missing_marker_count,uint32_t(__builtin_popcount(uint32_t(missing))));error(H1FError::MissingMarker); }
  if (r->presence[i]&uint8_t(~needed)) error(H1FError::UnexpectedMarker);
 }
 if (m.counters[ix(H1FCounter::clock_read_count)]>1192) error(H1FError::Counts);
 m.state=uint32_t(m.error?H1FState::Failed:H1FState::Complete);
 account(8); // state4 + accounting write4; header finalization is outside this record ledger
 if (m.error && !c.error)c.error=m.error;
 ++c.stored;
 if (!m.error) { if (c.kind==uint32_t(H1FKind::Campaign) && m.ordinal<5)++c.warmups;else ++c.measured; }
 c.finalFaults=faults;c.finalLeaseValid=lease;c.active=nullptr;
}
void h1FEndSet(uint32_t irqs) { control().irqAfter=irqs;control().active=nullptr;control().running=0; }
void h1FCalibrate(uint32_t irq,uint32_t selector,uint32_t cache) {
 auto &c=control();c.calibrationIrqBefore=irq;c.cacheSelector=selector;c.cacheControl=cache;
 std::memset(c.calibrationPresence,0,sizeof(c.calibrationPresence));
 for (uint32_t i=0;i<20;++i) {
  const uint64_t begin=k_cycle_get_64();
  const uint64_t captured=k_cycle_get_64();
  c.calibration[i].captured=captured;c.calibrationPresence[i/8]|=uint8_t(1u<<(i&7));++c.calibrationStores;c.calibrationBytes+=17;
  const uint64_t end=k_cycle_get_64();
  c.calibration[i].begin=begin;c.calibration[i].end=end;++c.calibrationCount;
 }
}
H1FError h1FCheckOwner(uint64_t a,uint64_t b,uint64_t epoch,uint64_t id,bool discover) {
 const auto &c=control();
 if (a!=c.nonce0 || b!=c.nonce1 || epoch!=c.ownerEpoch) return H1FError::Authentication;
 if (!c.campaignId || ((!discover || id) && id!=c.campaignId)) return H1FError::StaleCampaign;
 if (c.running || c.active) return H1FError::Held;
 return H1FError::None;
}
H1FError h1FReadRange(uint32_t ordinal,uint32_t offset,uint32_t n,const uint8_t *&bytes) {
 const auto &c=control();bytes=nullptr;
 if (c.running || c.active) return H1FError::Held;
 if (!n || n>1024 || ordinal>=c.stored || ordinal>=25 || offset>32768 || n>32768-offset) return H1FError::Bounds;
 bytes=reinterpret_cast<const uint8_t *>(&h1FrontendObserverStorage.records[ordinal])+offset;return H1FError::None;
}
void h1FExported(uint32_t ordinal,uint32_t offset,uint32_t n,bool sent) {
 auto &c=control();if (!sent || ordinal!=c.collectOrdinal || offset!=c.collectOffset) return;
 c.collectOffset+=n;
 if (c.collectOffset==32768) { c.collectOffset=0;++c.collectOrdinal; }
 if (c.collectOrdinal==c.stored && c.stored==c.requested && !c.error)c.held=0;
}
