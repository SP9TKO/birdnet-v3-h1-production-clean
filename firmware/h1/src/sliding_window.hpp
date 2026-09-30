#pragma once

#include <cstdint>

class H1SlidingWindowTracker {
public:
	H1SlidingWindowTracker(uint32_t windowSamples, uint32_t strideSamples);

	void reset();
	void observe(uint64_t totalSamples);
	bool bounds(uint64_t sequence, uint64_t &startSample,
		    uint64_t &endSample) const;
	bool hasCompleteWindow() const;
	uint64_t completeWindows() const;
	uint64_t latestSequence() const;
	uint64_t latestStartSample() const;
	uint64_t latestEndSample() const;
	uint64_t geometryFaults() const;

private:
	uint32_t windowSamples_;
	uint32_t strideSamples_;
	uint64_t nextSequence_;
	uint64_t nextStartSample_;
	uint64_t completeWindows_;
	uint64_t latestStartSample_;
	uint64_t geometryFaults_;
};
