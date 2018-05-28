#ifndef BASE64_H
#define BASE64_H

#include <buffer/Buffer.h>

namespace reckoning {
namespace util {
namespace base64 {

inline size_t encode(const uint8_t* input, size_t inputSize, uint8_t* output, size_t outputSize)
{
    if (!input || !output)
        return false;
    size_t inflation = static_cast<size_t>(inputSize / 3) * 4;
    if (inputSize % 3)
        inflation += 4;
    if (outputSize < inflation)
        return 0;
    static const char* base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    size_t inlen = inputSize;
    const uint8_t* in = input;
    uint8_t* out = output;

    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (inlen--) {
        char_array_3[i++] = *(in++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for(i = 0; (i <4) ; i++)
                *(out++) = base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i)
    {
        for(j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; (j < i + 1); j++)
            *(out++) = base64_chars[char_array_4[j]];

        while((i++ < 3)) {
            printf("assigning equal\n");
            *(out++) = '=';
        }

    }

    return out - output;
}

}}} // namespace reckoning::util::base64

#endif // BASE64_H
