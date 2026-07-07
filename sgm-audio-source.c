/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Noro
 *
 * This file is part of the StreamGearMixer OBS plugin (sgm-audio-source).
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <https://www.gnu.org/licenses/> or the bundled
 * LICENSE file.
 *
 * This plugin links against libobs, which is licensed under the GNU General
 * Public License v2. It is therefore distributed under GPL-2.0-or-later.
 */

/*
 * sgm-audio-source — StreamGearMixer audio source plugin for OBS Studio
 *
 * StreamGearMixer が名前付き共有メモリ "Local\StreamGearMixerAudioV1" に
 * 書き出す STREAM ミックスを読み取り、OBS の音声入力ソースとして出力する。
 * 仮想オーディオケーブル不要で StreamGearMixer → OBS に音を渡せる。
 *
 * メモリレイアウトはミキサー側 Source/SharedAudioSender.h と一致させること。
 *
 * Reader side rules:
 *   - The mapping is opened READ-ONLY; write_pos is read as a plain aligned
 *     64-bit volatile load (atomic on x64) followed by a barrier.
 *   - If write_pos goes backwards, the mixer restarted -> resync.
 *   - If we fall too far behind, jump forward (drop) instead of drifting.
 */

#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <obs-module.h>
#include <util/platform.h>

OBS_DECLARE_MODULE()

#define SGM_SHM_NAME     L"Local\\StreamGearMixerAudioV1"
#define SGM_MAGIC        0x53474D58u /* 'SGMX' */
#define SGM_DATA_OFFSET  4096u
#define SGM_CHUNK_FRAMES 4096u

#pragma pack(push, 8)
struct sgm_shm_header {
	uint32_t magic;
	uint32_t version;
	uint32_t sample_rate;
	uint32_t channels;
	uint32_t ring_frames;
	uint32_t reserved[9];
	volatile LONG64 write_pos; /* total frames written (monotonic) */
};
#pragma pack(pop)

struct sgm_source {
	obs_source_t *source;

	HANDLE mapping;
	uint8_t *view;
	volatile struct sgm_shm_header *hdr;
	const volatile float *ring; /* interleaved L R L R ... */

	int64_t read_pos;
	bool attached;

	HANDLE thread;
	volatile bool stop;
	float *chunk; /* SGM_CHUNK_FRAMES * 2 floats */
};

static void sgm_detach(struct sgm_source *s)
{
	s->attached = false;
	s->hdr = NULL;
	s->ring = NULL;
	if (s->view) {
		UnmapViewOfFile(s->view);
		s->view = NULL;
	}
	if (s->mapping) {
		CloseHandle(s->mapping);
		s->mapping = NULL;
	}
}

static bool sgm_try_attach(struct sgm_source *s)
{
	HANDLE m = OpenFileMappingW(FILE_MAP_READ, FALSE, SGM_SHM_NAME);
	if (!m)
		return false;

	uint8_t *v = MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
	if (!v) {
		CloseHandle(m);
		return false;
	}

	volatile struct sgm_shm_header *hdr = (volatile struct sgm_shm_header *)v;
	const uint32_t rf = hdr->ring_frames;
	if (hdr->magic != SGM_MAGIC || hdr->channels != 2 || rf == 0 ||
	    (rf & (rf - 1)) != 0) {
		UnmapViewOfFile(v);
		CloseHandle(m);
		return false;
	}

	s->mapping = m;
	s->view = v;
	s->hdr = hdr;
	s->ring = (const volatile float *)(v + SGM_DATA_OFFSET);
	s->read_pos = hdr->write_pos; /* start live */
	MemoryBarrier();
	s->attached = true;

	blog(LOG_INFO,
	     "[sgm-audio-source] attached to StreamGearMixer (sr=%u ring=%u)",
	     hdr->sample_rate, rf);
	return true;
}

static DWORD WINAPI sgm_thread(LPVOID param)
{
	struct sgm_source *s = param;
	uint64_t next_attach_try = 0;

	while (!s->stop) {
		if (!s->attached) {
			const uint64_t now = os_gettime_ns();
			if (now >= next_attach_try) {
				next_attach_try = now + 1000000000ULL;
				sgm_try_attach(s);
			}
			if (!s->attached) {
				os_sleep_ms(100);
				continue;
			}
		}

		volatile struct sgm_shm_header *hdr = s->hdr;
		const uint32_t ring_frames = hdr->ring_frames;
		const uint32_t sr = hdr->sample_rate;
		if (hdr->magic != SGM_MAGIC || ring_frames == 0 ||
		    (ring_frames & (ring_frames - 1)) != 0 || sr < 8000 ||
		    sr > 384000) {
			os_sleep_ms(50);
			continue;
		}

		const int64_t w = hdr->write_pos; /* aligned 64-bit load */
		MemoryBarrier();                  /* data written before write_pos is visible */

		if (w < s->read_pos)
			s->read_pos = w; /* mixer restarted */
		if (w - s->read_pos > (int64_t)(ring_frames * 3u / 4u))
			s->read_pos = w - (int64_t)(ring_frames / 4u); /* fell behind */

		int64_t avail = w - s->read_pos;
		if (avail <= 0) {
			os_sleep_ms(4);
			continue;
		}

		while (avail > 0 && !s->stop) {
			const uint32_t n =
				(uint32_t)(avail > (int64_t)SGM_CHUNK_FRAMES
						   ? SGM_CHUNK_FRAMES
						   : avail);
			const uint32_t idx =
				(uint32_t)s->read_pos & (ring_frames - 1);
			uint32_t first = ring_frames - idx;
			if (first > n)
				first = n;

			memcpy(s->chunk, (const void *)(s->ring + (size_t)idx * 2),
			       (size_t)first * 2 * sizeof(float));
			if (n > first)
				memcpy(s->chunk + (size_t)first * 2,
				       (const void *)s->ring,
				       (size_t)(n - first) * 2 * sizeof(float));

			struct obs_source_audio a = {0};
			a.data[0] = (const uint8_t *)s->chunk;
			a.frames = n;
			a.speakers = SPEAKERS_STEREO;
			a.format = AUDIO_FORMAT_FLOAT;
			a.samples_per_sec = sr;
			a.timestamp = os_gettime_ns() -
				      (uint64_t)n * 1000000000ULL / sr;
			obs_source_output_audio(s->source, &a);

			s->read_pos += n;
			avail -= n;
		}

		os_sleep_ms(4);
	}
	return 0;
}

static const char *sgm_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "StreamGearMixer";
}

static void *sgm_create(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(settings);

	struct sgm_source *s = bzalloc(sizeof(struct sgm_source));
	s->source = source;
	s->chunk = bmalloc(SGM_CHUNK_FRAMES * 2 * sizeof(float));
	s->thread = CreateThread(NULL, 0, sgm_thread, s, 0, NULL);
	return s;
}

static void sgm_destroy(void *data)
{
	struct sgm_source *s = data;
	if (!s)
		return;

	s->stop = true;
	if (s->thread) {
		WaitForSingleObject(s->thread, 5000);
		CloseHandle(s->thread);
	}
	sgm_detach(s);
	bfree(s->chunk);
	bfree(s);
}

static struct obs_source_info sgm_source_info;

bool obs_module_load(void)
{
	sgm_source_info.id = "sgm_stream_audio_source";
	sgm_source_info.type = OBS_SOURCE_TYPE_INPUT;
	sgm_source_info.output_flags = OBS_SOURCE_AUDIO;
	sgm_source_info.get_name = sgm_get_name;
	sgm_source_info.create = sgm_create;
	sgm_source_info.destroy = sgm_destroy;
	sgm_source_info.icon_type = OBS_ICON_TYPE_AUDIO_OUTPUT;
	obs_register_source(&sgm_source_info);

	blog(LOG_INFO, "[sgm-audio-source] StreamGearMixer audio source loaded");
	return true;
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "StreamGearMixer audio source (shared-memory link, no virtual cable)";
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return "StreamGearMixer Audio Source";
}
