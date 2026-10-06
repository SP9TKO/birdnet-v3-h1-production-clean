#include "classifier_bridge.hpp"
#include <cstddef>
#include <cstdint>
extern "C" int bridge_cases(const uint32_t *source,const uint32_t *scale,
                             int16_t *output,int8_t *saturation,size_t count)
{
    for(size_t i=0;i<count;++i) {
        if(!birdnet::h1::quantizeClassifierInputBits(source[i],scale[i],output[i],saturation[i]))
            return int(i+1);
    }
    return 0;
}
extern "C" int bridge_vector(const float *source,int16_t *output,size_t count,
                              size_t *low,size_t *high)
{
    return birdnet::h1::quantizeClassifierInput(source,output,count,*low,*high) ? 0 : 1;
}
