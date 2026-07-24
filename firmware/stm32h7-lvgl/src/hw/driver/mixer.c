/*
 * mixer.c
 *
 *  Created on: 2020. 8. 8.
 *      Author: Baram
 */




#include "mixer.h"



#ifdef _USE_HW_MIXER



static uint32_t mixerBufAvailable(mixer_t *p_mixer, uint8_t ch);
static int16_t mixerBufRead(mixer_t *p_mixer, uint8_t ch);




bool mixerInit(mixer_t *p_mixer)
{
  uint32_t i;


  for (i=0; i<MIXER_MAX_CH; i++)
  {
    p_mixer->buf[i].in = 0;
    p_mixer->buf[i].out = 0;
    p_mixer->buf[i].length = MIXER_MAX_BUF_LEN;
  }
  p_mixer->volume = 100;

  return true;
}

bool mixerSetVolume(mixer_t *p_mixer, int32_t volume)
{
  p_mixer->volume = constrain(volume, 0, 100);
  return true;
}

int32_t mixerGetVolume(mixer_t *p_mixer)
{
  return p_mixer->volume;
}

bool mixerWrite(mixer_t *p_mixer, uint8_t ch, int16_t *p_data, uint32_t length)
{
  bool ret = true;
  uint32_t index;
  uint32_t next_index;
  uint32_t i;

  if (ch >= MIXER_MAX_CH) return false;

  for (i=0; i<length; i++)
  {
    index      = p_mixer->buf[ch].in;
    next_index = p_mixer->buf[ch].in + 1;

    if (next_index == p_mixer->buf[ch].length)
    {
      next_index = 0;;
    }

    if (next_index != p_mixer->buf[ch].out)
    {
      // p_mixer->buf[ch].buf[index] = p_data[i] * p_mixer->volume / 100;
      p_mixer->buf[ch].buf[index] = p_data[i];
      p_mixer->buf[ch].in         = next_index;
    }
    else
    {
      ret = false; // ERR_FULL
      break;
    }
  }

  return ret;
}


//  http://atastypixel.com/blog/how-to-mix-audio-samples-properly-on-ios/
//
int16_t mixerSamples(int16_t a, int16_t b)
{
 return
    // If both samples are negative, mixed signal must have an amplitude between the lesser of A and B, and the minimum permissible negative amplitude
    a < 0 && b < 0 ?
        ((int)a + (int)b) - (((int)a * (int)b)/INT16_MIN) :

    // If both samples are positive, mixed signal must have an amplitude between the greater of A and B, and the maximum permissible positive amplitude
    ( a > 0 && b > 0 ?
        ((int)a + (int)b) - (((int)a * (int)b)/INT16_MAX)

    // If samples are on opposite sides of the 0-crossing, mixed signal should reflect that samples cancel each other out somewhat
    :
        a + b);
}

bool mixerRead(mixer_t *p_mixer, int16_t *p_data, uint32_t length)
{
  uint32_t i;
  uint32_t ch;
  int16_t mixer_out;
  int16_t sample;

  for (i=0; i<length; i++)
  {

    mixer_out = mixerBufRead(p_mixer, 0);
    for (ch=1; ch<MIXER_MAX_CH; ch++)
    {
      sample = mixerBufRead(p_mixer, ch);
      mixer_out = mixerSamples(mixer_out, sample);
    }

    p_data[i] = mixer_out * p_mixer->volume / 100;;
  }

  return true;
}

uint32_t mixerBufAvailable(mixer_t *p_mixer, uint8_t ch)
{
  uint32_t ret = 0;

  if (ch >= MIXER_MAX_CH) return 0;

  ret = (p_mixer->buf[ch].length + p_mixer->buf[ch].in - p_mixer->buf[ch].out) % p_mixer->buf[ch].length;

  return ret;
}

int16_t mixerBufRead(mixer_t *p_mixer, uint8_t ch)
{
  int16_t ret = 0;
  uint32_t index;
  uint32_t next_index;

  if (ch >= MIXER_MAX_CH) return 0;


  index      = p_mixer->buf[ch].out;
  next_index = p_mixer->buf[ch].out + 1;

  if (next_index == p_mixer->buf[ch].length)
  {
    next_index = 0;
  }

  if (index != p_mixer->buf[ch].in)
  {
    ret = p_mixer->buf[ch].buf[index];
    p_mixer->buf[ch].out = next_index;
  }

  return ret;
}

uint32_t mixerAvailable(mixer_t *p_mixer)
{
  uint32_t ret = 0;
  uint32_t i;
  uint32_t buf_len;

  for (i=0; i<MIXER_MAX_CH; i++)
  {
    buf_len = mixerBufAvailable(p_mixer, i);
    if (buf_len > ret)
    {
      ret = buf_len;
    }
  }


  return ret;
}

uint32_t mixerAvailableForWrite(mixer_t *p_mixer, uint8_t ch)
{
  uint32_t rx_len;
  uint32_t wr_len;

  if (ch >= MIXER_MAX_CH) return 0;

  rx_len = mixerBufAvailable(p_mixer, ch);
  wr_len = (p_mixer->buf[ch].length - 1) - rx_len;

  return wr_len;
}

bool mixerIsEmpty(mixer_t *p_mixer, uint8_t ch)
{
  if (mixerBufAvailable(p_mixer, ch) > 0) 
    return false;
  else                                    
    return true;
}

int8_t mixerGetEmptyChannel(mixer_t *p_mixer)
{
  int8_t ret = -1;
  uint32_t i;

  for (i=0; i<MIXER_MAX_CH; i++)
  {
    if (mixerBufAvailable(p_mixer, i) == 0)
    {
      ret = i;
      break;
    }
  }

  return ret;
}

int8_t mixerGetValidChannel(mixer_t *p_mixer, uint32_t length)
{
  int8_t ret = -1;
  uint32_t i;

  for (i=0; i<MIXER_MAX_CH; i++)
  {
    if (mixerAvailableForWrite(p_mixer, i) >= length)
    {
      ret = i;
    }
  }

  return ret;
}


#endif