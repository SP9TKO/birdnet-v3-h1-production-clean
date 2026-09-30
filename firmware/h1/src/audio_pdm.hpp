#pragma once

#include "audio_contract.hpp"

#include <cstddef>
#include <cstdint>

int h1AudioPdmInit();
bool h1AudioPdmStart(H1MicMode mode, H1MicError &error);
bool h1AudioPdmStop(H1MicError &error);
H1MicStatusSnapshot h1AudioPdmStatus();
bool h1AudioPdmSelectWindow(uint64_t sequence, H1SelectedWindowInfo &info,
			    H1MicError &error);
bool h1AudioPdmCopySelectedBytes(uint32_t offsetBytes, void *destination,
				 uint32_t bytes, H1MicError &error);
bool h1AudioPdmPrepareSelectedWaveform(float *destination, size_t elements,
				       uint32_t &floatCrc32,
				       char floatSha256[65],
				       H1SelectedWindowInfo &info,
				       H1MicError &error);
void h1AudioPdmSetInferenceState(H1MicInferenceState state);
