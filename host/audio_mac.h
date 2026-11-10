/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HENGEVM_AUDIO_MAC_H
#define HENGEVM_AUDIO_MAC_H

#include "hda.h"
#include "virtio_snd.h"

/* Attach CoreAudio playback (RemoteIO / DefaultOutput) + microphone capture (AudioQueueInput)
 * to the guest's Intel HD Audio duplex controller (macOS and iOS). */
void hv_audio_attach_hda(HvHda *hda);
void hv_audio_shutdown(void);

/* Legacy virtio-snd attachment retained for compatibility. */
void hv_audio_attach(HvVirtioSnd *d);

#endif
