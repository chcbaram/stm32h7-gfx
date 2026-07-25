#ifndef CASSETTE_AUDIO_H_
#define CASSETTE_AUDIO_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"


#ifdef _USE_HW_LVGL

#define CASS_SPECTRUM_BARS   13


typedef enum
{
  CASS_AUDIO_IDLE = 0,
  CASS_AUDIO_PLAY,
  CASS_AUDIO_REC,
} CassAudioState_t;


bool cassetteAudioInit(void);

/* SD 의 wav 목록을 훑는다. 반환값은 찾은 개수. */
int         cassetteAudioScan(void);
int         cassetteAudioCount(void);
const char *cassetteAudioName(int idx);
uint32_t    cassetteAudioSize(int idx);   /* 파일 크기 bytes */

bool cassetteAudioPlay(int idx);
bool cassetteAudioRecord(void);   /* 새 테이프에 녹음 */
bool cassetteAudioStop(void);

CassAudioState_t cassetteAudioGetState(void);
int      cassetteAudioGetIndex(void);
uint32_t cassetteAudioGetPosMs(void);
uint32_t cassetteAudioGetDurMs(void);

/* 스펙트럼 막대 0..100 을 채운다. 반환값은 채운 개수. */
int cassetteAudioGetSpectrum(uint8_t *p_bars, int max_bars);

/* 볼륨 0..100 */
int  cassetteAudioGetVolume(void);
void cassetteAudioSetVolume(int vol);

#endif

#ifdef __cplusplus
}
#endif

#endif
