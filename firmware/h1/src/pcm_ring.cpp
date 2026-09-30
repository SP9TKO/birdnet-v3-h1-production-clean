#include "pcm_ring.hpp"

#include <algorithm>
#include <cstring>

H1PcmRing::H1PcmRing(int16_t *storage, uint32_t capacitySamples)
	: storage_(storage), capacity_(capacitySamples), totalSamples_(0),
	  writeIndex_(0), consumerActive_(false), consumerFrontier_(0),
	  frontierFaults_(0)
{
}

void H1PcmRing::reset()
{
	totalSamples_ = 0;
	writeIndex_ = 0;
	consumerActive_ = false;
	consumerFrontier_ = 0;
	frontierFaults_ = 0;
}

bool H1PcmRing::commit(const int16_t *samples, uint32_t count)
{
	if (!storage_ || !samples || count == 0 || count > capacity_) {
		++frontierFaults_;
		return false;
	}
	const uint64_t newTotal = totalSamples_ + count;
	const uint64_t oldestAfter = newTotal > capacity_ ? newTotal - capacity_ : 0;
	if (consumerActive_ && oldestAfter > consumerFrontier_) {
		++frontierFaults_;
		return false;
	}

	const uint32_t first = std::min(count, capacity_ - writeIndex_);
	memcpy(storage_ + writeIndex_, samples, size_t(first) * sizeof(int16_t));
	if (first != count) {
		memcpy(storage_, samples + first,
		       size_t(count - first) * sizeof(int16_t));
	}
	writeIndex_ = (writeIndex_ + count) % capacity_;
	totalSamples_ = newTotal;
	return true;
}

bool H1PcmRing::copyRange(uint64_t startSample, uint32_t count,
			  int16_t *destination) const
{
	if (!destination || count == 0 || count > capacity_ ||
	    startSample < oldestRetainedSample() || startSample > totalSamples_ ||
	    uint64_t(count) > totalSamples_ - startSample) {
		return false;
	}
	const uint32_t start = physicalIndex(startSample);
	const uint32_t first = std::min(count, capacity_ - start);
	memcpy(destination, storage_ + start, size_t(first) * sizeof(int16_t));
	if (first != count) {
		memcpy(destination + first, storage_,
		       size_t(count - first) * sizeof(int16_t));
	}
	return true;
}

void H1PcmRing::beginConsumer(uint64_t readFrontier)
{
	consumerActive_ = true;
	consumerFrontier_ = readFrontier;
}

void H1PcmRing::advanceConsumer(uint64_t readFrontier)
{
	if (consumerActive_ && readFrontier > consumerFrontier_) {
		consumerFrontier_ = readFrontier;
	}
}

void H1PcmRing::endConsumer()
{
	consumerActive_ = false;
	consumerFrontier_ = totalSamples_;
}

uint64_t H1PcmRing::totalSamples() const
{
	return totalSamples_;
}

uint64_t H1PcmRing::oldestRetainedSample() const
{
	return totalSamples_ > capacity_ ? totalSamples_ - capacity_ : 0;
}

uint32_t H1PcmRing::writeIndex() const
{
	return writeIndex_;
}

uint32_t H1PcmRing::capacity() const
{
	return capacity_;
}

uint64_t H1PcmRing::frontierFaults() const
{
	return frontierFaults_;
}

uint32_t H1PcmRing::physicalIndex(uint64_t absoluteSample) const
{
	return uint32_t(absoluteSample % capacity_);
}

bool H1PcmRing::rangeWraps(uint64_t startSample, uint32_t count) const
{
	return uint64_t(physicalIndex(startSample)) + count > capacity_;
}
