#pragma once

#include <cstddef>
#include <cstdint>

class H1PcmRing {
public:
	H1PcmRing(int16_t *storage, uint32_t capacitySamples);

	void reset();
	bool commit(const int16_t *samples, uint32_t count);
	bool copyRange(uint64_t startSample, uint32_t count, int16_t *destination) const;
	void beginConsumer(uint64_t readFrontier);
	void advanceConsumer(uint64_t readFrontier);
	void endConsumer();

	uint64_t totalSamples() const;
	uint64_t oldestRetainedSample() const;
	uint32_t writeIndex() const;
	uint32_t capacity() const;
	uint64_t frontierFaults() const;
	uint32_t physicalIndex(uint64_t absoluteSample) const;
	bool rangeWraps(uint64_t startSample, uint32_t count) const;

private:
	int16_t *storage_;
	uint32_t capacity_;
	uint64_t totalSamples_;
	uint32_t writeIndex_;
	bool consumerActive_;
	uint64_t consumerFrontier_;
	uint64_t frontierFaults_;
};
