#include "sliding_window.hpp"

H1SlidingWindowTracker::H1SlidingWindowTracker(uint32_t windowSamples,
					       uint32_t strideSamples)
	: windowSamples_(windowSamples), strideSamples_(strideSamples),
	  nextSequence_(0), nextStartSample_(0), completeWindows_(0),
	  latestStartSample_(0), geometryFaults_(0)
{
}

void H1SlidingWindowTracker::reset()
{
	nextSequence_ = 0;
	nextStartSample_ = 0;
	completeWindows_ = 0;
	latestStartSample_ = 0;
	geometryFaults_ = 0;
}

void H1SlidingWindowTracker::observe(uint64_t totalSamples)
{
	while (nextStartSample_ <= totalSamples &&
	       uint64_t(windowSamples_) <= totalSamples - nextStartSample_) {
		const uint64_t expectedStart = nextSequence_ * strideSamples_;
		if (nextStartSample_ != expectedStart ||
		    (completeWindows_ != 0 &&
		     nextStartSample_ - latestStartSample_ != strideSamples_)) {
			++geometryFaults_;
		}
		latestStartSample_ = nextStartSample_;
		++completeWindows_;
		++nextSequence_;
		nextStartSample_ += strideSamples_;
	}
}

bool H1SlidingWindowTracker::bounds(uint64_t sequence, uint64_t &startSample,
				    uint64_t &endSample) const
{
	if (sequence >= completeWindows_) {
		return false;
	}
	startSample = sequence * strideSamples_;
	endSample = startSample + windowSamples_;
	return true;
}

bool H1SlidingWindowTracker::hasCompleteWindow() const
{
	return completeWindows_ != 0;
}

uint64_t H1SlidingWindowTracker::completeWindows() const
{
	return completeWindows_;
}

uint64_t H1SlidingWindowTracker::latestSequence() const
{
	return completeWindows_ == 0 ? 0 : completeWindows_ - 1;
}

uint64_t H1SlidingWindowTracker::latestStartSample() const
{
	return latestStartSample_;
}

uint64_t H1SlidingWindowTracker::latestEndSample() const
{
	return completeWindows_ == 0 ? 0 : latestStartSample_ + windowSamples_;
}

uint64_t H1SlidingWindowTracker::geometryFaults() const
{
	return geometryFaults_;
}
