//
// Created by Wenxuan Lin on 2025-02-23.
//

#include <string_view>

#include "silk.h"

#include <SKP_Silk_SigProc_FIX.h>

#include "SKP_Silk_control.h"
#include "SKP_Silk_SDK_API.h"
#include "SKP_Silk_typedef.h"

constexpr std::string_view silk_magic = "\x02#!SILK_V3";
constexpr SKP_int32 sample_rate = 24000;

int silk_decode(uint8_t* silk_data, int data_len, cb_codec callback, void* userdata) {
    SKP_uint8 payload[MAX_BYTES_PER_FRAME * MAX_INPUT_FRAMES * (MAX_LBRR_DELAY + 1)];
    SKP_uint8* payloadEnd = nullptr, * payloadToDec = nullptr;
    SKP_int16 nBytesPerPacket[MAX_LBRR_DELAY + 1];
    SKP_int32 remainPackets = 0;
    SKP_int16 len, totalLen = 0, nBytes = 0;
    SKP_int32 decSizeBytes;
    uint8_t* psRead = silk_data;
    void* psDec = nullptr;

    SKP_SILK_SDK_DecControlStruct dec_control;

    if (memcmp(psRead, silk_magic.data(), std::size(silk_magic)) != 0) {
        return 1;
    }

    psRead += std::size(silk_magic);

    /* Create decoder */
    SKP_int32 result = SKP_Silk_SDK_Get_Decoder_Size(&decSizeBytes);
    if (result) {
        return 1;
    }

    psDec = malloc(decSizeBytes);
    result = SKP_Silk_SDK_InitDecoder(psDec);
    if (result) {
        free(psDec);
        return 1;
    }

    payloadEnd = payload;
    dec_control.framesPerPacket = 1;
    dec_control.API_sampleRate = sample_rate;

    for (int i = 0; i < MAX_LBRR_DELAY; i++) {
        nBytes = *reinterpret_cast<short*>(psRead); // read size of payload
        psRead += sizeof(SKP_int16);

#ifdef _SYSTEM_IS_BIG_ENDIAN
        swap_endian(&nBytes, 1);
#endif

        memcpy(payloadEnd, psRead, nBytes); // read payload
        psRead += sizeof(SKP_uint8) * nBytes;

        nBytesPerPacket[i] = nBytes;
        payloadEnd += nBytes;
    }

    nBytesPerPacket[MAX_LBRR_DELAY] = 0;

    while (true) {
        SKP_int16 out[(FRAME_LENGTH_MS * MAX_API_FS_KHZ << 1) * MAX_INPUT_FRAMES];
        if (remainPackets == 0) {
            nBytes = *reinterpret_cast<short *>(psRead); // Read payload size
            psRead += sizeof(SKP_int16);

#ifdef _SYSTEM_IS_BIG_ENDIAN
            swap_endian(&nBytes, 1);
#endif

            if (nBytes < 0 || psRead - silk_data >= data_len) {
                remainPackets = MAX_LBRR_DELAY;
                goto decode;
            }

            memcpy(payloadEnd, psRead, nBytes); // read payload
            psRead += sizeof(SKP_uint8) * nBytes;

        }
        else if (--remainPackets <= 0) {
            break;
        }

        decode:
          if (nBytesPerPacket[0] != 0) {
              nBytes = nBytesPerPacket[0];
              payloadToDec = payload;
          }

        SKP_int16* outPtr = out;
        totalLen = 0;
        int frames = 0;

        do { /* Decode all frames in the packet */
            SKP_Silk_SDK_Decode(psDec, &dec_control, 0, payloadToDec, nBytes, outPtr, &len);  /* Decode 20 ms */

            frames++;
            outPtr += len;
            totalLen += len;

            if (frames > MAX_INPUT_FRAMES) { /* Hack for corrupt stream that could generate too many frames */
                outPtr = out;
                totalLen = 0;
                frames = 0;
            }
        } while (dec_control.moreInternalDecoderFrames);

        /* Write output to file */
#ifdef _SYSTEM_IS_BIG_ENDIAN
        swap_endian(out, totalLen);
#endif

        callback(userdata, reinterpret_cast<uint8_t*>(out), sizeof(SKP_int16) * totalLen);

        /* Update buffer */
        SKP_int16 totBytes = 0;
        for (int i = 0; i < MAX_LBRR_DELAY; i++) {
            totBytes += nBytesPerPacket[i + 1];
        }

        if (totBytes < 0 || totBytes > sizeof(payload)) { /* Check if the received totBytes is valid */
            free(psDec);
            return 1;
        }

        SKP_memmove(payload, &payload[nBytesPerPacket[0]], totBytes * sizeof(SKP_uint8));
        payloadEnd -= nBytesPerPacket[0];
        SKP_memmove(nBytesPerPacket, &nBytesPerPacket[1], MAX_LBRR_DELAY * sizeof(SKP_int16));
    }

    free(psDec);
    return 0;
}

int silk_encode(uint8_t* pcm_data, int data_len, cb_codec callback, void* userdata) {
    size_t counter;
    SKP_int16 n_bytes;
    SKP_uint8 payload[MAX_BYTES_PER_FRAME * MAX_INPUT_FRAMES];
    SKP_int16 in[FRAME_LENGTH_MS * MAX_API_FS_KHZ * MAX_INPUT_FRAMES];
    SKP_int32 enc_size_bytes, result;
    unsigned char* ps_read = pcm_data, *ps_read_end = pcm_data + data_len;
    void* ps_enc = nullptr;

#ifdef _SYSTEM_IS_BIG_ENDIAN
    SKP_int16 n_bytes_le;
#endif

    // Default settings
    SKP_int32 api_fs_hz = sample_rate;
    SKP_int32 max_internal_fs_hz = 0;
    SKP_int32 target_rate_bps = 24000;
    SKP_int32 smpls_since_last_packet, packet_size_ms = 20;

    if (max_internal_fs_hz == 0) {
        max_internal_fs_hz = 24000;
        if (api_fs_hz < max_internal_fs_hz) {
            max_internal_fs_hz = api_fs_hz;
        }
    }

#if LOW_COMPLEXITY_ONLY
    SKP_int32 complexity_mode = 0;
#else
    SKP_int32 complexity_mode = 2;
#endif

    SKP_SILK_SDK_EncControlStruct enc_status = { }; // Struct for status of encoder
    SKP_SILK_SDK_EncControlStruct enc_control = { }; // Struct for input to encoder

    enc_control.API_sampleRate = sample_rate;
    enc_control.maxInternalSampleRate = max_internal_fs_hz;
    enc_control.packetSize = (packet_size_ms * sample_rate) / 1000;
    enc_control.packetLossPercentage = 0;
    enc_control.useInBandFEC = 0;
    enc_control.useDTX = 0;
    enc_control.complexity = complexity_mode;
    enc_control.bitRate = (target_rate_bps > 0 ? target_rate_bps : 0);

    if (api_fs_hz > MAX_API_FS_KHZ * 1000 || api_fs_hz < 0) {
        return 1;
    }

    callback(userdata, reinterpret_cast<const std::uint8_t*>(silk_magic.data()), silk_magic.size());

    result = SKP_Silk_SDK_Get_Encoder_Size(&enc_size_bytes);
    if (result) {
        return 1;
    }

    ps_enc = malloc(enc_size_bytes);
    result = SKP_Silk_SDK_InitEncoder(ps_enc, &enc_status);
    if (result) {
        free(ps_enc);
        return 1;
    }

    smpls_since_last_packet = 0;

    while (true) {
        SKP_int32 frame_size_read_from_file_ms = 20;
        if (ps_read - pcm_data >= data_len) {
            break;
        }

        counter = frame_size_read_from_file_ms * api_fs_hz / 1000;
        if (counter * sizeof(SKP_int16) > ps_read_end - ps_read) {
            memset(in, 0x00, sizeof(in));
            size_t realrd = (ps_read_end - ps_read);
            memcpy(in, ps_read, realrd);
            ps_read += realrd;
        } else {
            size_t realrd = counter * sizeof(SKP_int16);
            memcpy(in, ps_read, realrd);
            ps_read += realrd;
        }

#ifdef _SYSTEM_IS_BIG_ENDIAN
        swap_endian(in, counter);
#endif

        n_bytes = MAX_BYTES_PER_FRAME * MAX_INPUT_FRAMES;
        SKP_Silk_SDK_Encode(ps_enc, &enc_control, in, static_cast<short>(counter), payload, &n_bytes);
        packet_size_ms = 1000 * enc_control.packetSize / enc_control.API_sampleRate;

        smpls_since_last_packet += static_cast<int>(counter);
        if (1000 * smpls_since_last_packet / api_fs_hz == packet_size_ms) {
            // Write payload size
#ifdef _SYSTEM_IS_BIG_ENDIAN
            n_bytes_le = n_bytes;
            swap_endian(&n_bytes_le, 1);
            callback(userdata, (void*)&n_bytes_le, sizeof(SKP_int16));
#else
            callback(userdata, static_cast<uint8_t *>((void*)&n_bytes), sizeof(SKP_int16));
#endif
            // Write payload
            callback(userdata, payload, sizeof(SKP_uint8) * n_bytes);

            smpls_since_last_packet = 0;
        }
    }

    free(ps_enc);
    return 0;
}