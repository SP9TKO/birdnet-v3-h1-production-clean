#include "sha256.hpp"

#include <tinycrypt/constants.h>
#include <tinycrypt/sha256.h>

void h1Sha256Hex(const void *data, size_t bytes, char output[65])
{
	tc_sha256_state_struct state{};
	uint8_t digest[32];
	if ((!data && bytes != 0) || !output ||
	    tc_sha256_init(&state) != TC_CRYPTO_SUCCESS ||
	    tc_sha256_update(&state, static_cast<const uint8_t *>(data), bytes) !=
		    TC_CRYPTO_SUCCESS ||
	    tc_sha256_final(digest, &state) != TC_CRYPTO_SUCCESS) {
		if (output) {
			output[0] = '\0';
		}
		return;
	}
	static constexpr char kDigits[] = "0123456789abcdef";
	for (size_t index = 0; index < sizeof(digest); ++index) {
		output[2 * index] = kDigits[digest[index] >> 4];
		output[2 * index + 1] = kDigits[digest[index] & 0x0f];
	}
	output[64] = '\0';
}
