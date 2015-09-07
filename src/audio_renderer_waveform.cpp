// Copyright (c) 2010, Niels Martin Hansen
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

#include "audio_renderer_waveform.h"

#include "audio_colorscheme.h"
#include "options.h"

#include <libaegisub/audio/provider.h>

#include <algorithm>
#include <wx/dcmemory.h>

enum {
	/// Only render the peaks
	Waveform_MaxOnly = 0,
	/// Render the peaks and averages
	Waveform_MaxAvg,
	Waveform_Continuous
};

AudioWaveformRenderer::AudioWaveformRenderer(std::string const& color_scheme_name, bool separate_channels)
: render_averages(OPT_GET("Audio/Display/Waveform Style")->GetInt() == Waveform_MaxAvg)
{
	colors.reserve(AudioStyle_MAX);
	for (int i = 0; i < AudioStyle_MAX; ++i)
		colors.emplace_back(6, color_scheme_name, i);

  this->separate_channels = separate_channels;
}

AudioWaveformRenderer::~AudioWaveformRenderer() { }

void AudioWaveformRenderer::Render(wxBitmap &bmp, int start, AudioRenderingStyle style)
{
  if (separate_channels)
    RenderSeparate(bmp, start, style);
  else
    RenderSingle(bmp, start, style);
}

void AudioWaveformRenderer::RenderSingle(wxBitmap &bmp, int start, AudioRenderingStyle style)
{
	wxMemoryDC dc(bmp);
	wxRect rect(wxPoint(0, 0), bmp.GetSize());
	int midpoint = rect.height / 2;

	const AudioColorScheme *pal = &colors[style];

	double samplesPerPixel = pixel_ms * provider->GetSampleRate() / 1000.0;

	// Fill the background
	dc.SetBrush(wxBrush(pal->get(0.0f)));
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.DrawRectangle(rect);

	// Make sure we've got a buffer to fill with audio data
	if (!audio_buffer)
	{
		// Buffer for one pixel strip of audio
		size_t buffer_needed = samplesPerPixel * provider->GetChannels() * provider->GetBytesPerSample();
		audio_buffer.reset(new char[buffer_needed]);
	}

	double cur_sample_cursor = start * samplesPerPixel;

	assert(provider->GetBytesPerSample() == 2);

	wxPen pen_peaks(wxPen(pal->get(0.4f)));
	wxPen pen_avgs(wxPen(pal->get(0.7f)));

	for (int x = 0; x < rect.width; ++x)
	{
		provider->GetAudio(audio_buffer.get(), (int64_t)cur_sample_cursor, (int64_t) samplesPerPixel);
		cur_sample_cursor += samplesPerPixel;

		int peak_min = 0, peak_max = 0;
		int64_t avg_min_accum = 0, avg_max_accum = 0;
		auto aud = reinterpret_cast<const int16_t *>(audio_buffer.get());
		for (int si = samplesPerPixel; si > 0; --si, aud += provider->GetChannels())
		{
      // calculate mean over all channels (this replaces the downmix audio provider)
      int curSample = 0;
      /*
      for (int nc = 0; nc < provider->GetChannels(); ++nc)
        curSample += static_cast<int>(*(aud+nc));
      curSample /= provider->GetChannels();
      */
      curSample = *aud;

			if (curSample > 0)
			{
				peak_max = std::max(peak_max, curSample);
				avg_max_accum += curSample;
			}
			else
			{
				peak_min = std::min(peak_min, curSample);
				avg_min_accum += curSample;
			}
		}

		// midpoint is half height
		peak_min = std::max((int)(peak_min * amplitude_scale * midpoint) / 0x8000, -midpoint);
		peak_max = std::min((int)(peak_max * amplitude_scale * midpoint) / 0x8000, midpoint);
		int avg_min = std::max((int)(avg_min_accum * amplitude_scale * midpoint / samplesPerPixel) / 0x8000, -midpoint);
		int avg_max = std::min((int)(avg_max_accum * amplitude_scale * midpoint / samplesPerPixel) / 0x8000, midpoint);

		dc.SetPen(pen_peaks);
		dc.DrawPoint(x, midpoint - peak_max);
		dc.DrawPoint(x, midpoint - peak_min);
		//dc.DrawLine(x, midpoint - peak_max, x, midpoint - peak_min);
    /*
		if (render_averages) {
			dc.SetPen(pen_avgs);
			dc.DrawLine(x, midpoint - avg_max, x, midpoint - avg_min);
		}
    */
	}

  /*
	// Horizontal zero-point line
	if (render_averages)
		dc.SetPen(wxPen(pal->get(1.0f)));
	else
		dc.SetPen(pen_peaks);

	dc.DrawLine(0, midpoint, rect.width, midpoint);
  */
}

void AudioWaveformRenderer::RenderSeparate(wxBitmap &bmp, int start, AudioRenderingStyle style)
{
	wxMemoryDC dc(bmp);
	wxRect rect(wxPoint(0, 0), bmp.GetSize());

	const AudioColorScheme *pal = &colors[style];

	double samplesPerPixel = pixel_ms * provider->GetSampleRate() / 1000.0;

	// Fill the background
	dc.SetBrush(wxBrush(pal->get(0.0f)));
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.DrawRectangle(rect);


	// Make sure we've got a buffer to fill with audio data
	if (!audio_buffer)
	{
		// Buffer for one pixel strip of audio
		size_t buffer_needed = samplesPerPixel * provider->GetChannels() * provider->GetBytesPerSample();
		audio_buffer.reset(new char[buffer_needed]);
	}


	double cur_sample_cursor = start * samplesPerPixel;

	assert(provider->GetBytesPerSample() == 2);
	//assert(provider->GetChannels() == 1);
	int channels = provider->GetChannels();

	wxPen pen_peaks(wxPen(pal->get(0.4f)));
	wxPen pen_avgs(wxPen(pal->get(0.7f)));

	for (int x = 0; x < rect.width; ++x)
	{
		provider->GetAudio(audio_buffer.get(), (int64_t)cur_sample_cursor, (int64_t)samplesPerPixel);
		cur_sample_cursor += samplesPerPixel;

    for (int c = 0; c < channels; ++c)
    {
      // c * height / channels + height / channels / 2 = h / channels * (c + 0.5)
      //int midpoint = rect.height / 2;
      int midpoint = (int)(rect.height / (float) channels * (c + 0.5f));

      int peak_min = 0, peak_max = 0;
      int64_t avg_min_accum = 0, avg_max_accum = 0;
      auto aud = reinterpret_cast<const int16_t *>(audio_buffer.get());
      for (int si = samplesPerPixel; si > 0; --si, aud += channels)
      {
        if (*(aud+c) > 0)
        {
          peak_max = std::max(peak_max, (int)*(aud+c));
          avg_max_accum += *(aud+c);
        }
        else
        {
          peak_min = std::min(peak_min, (int)*(aud+c));
          avg_min_accum += *(aud+c);
        }
      }

      // midpoint is half height
      peak_min = std::max((int)(peak_min * amplitude_scale / channels * midpoint) / 0x8000, -midpoint);
      peak_max = std::min((int)(peak_max * amplitude_scale / channels * midpoint) / 0x8000, midpoint);
      int avg_min = std::max((int)(avg_min_accum * amplitude_scale / channels * midpoint / samplesPerPixel) / 0x8000, -midpoint);
      int avg_max = std::min((int)(avg_max_accum * amplitude_scale / channels * midpoint / samplesPerPixel) / 0x8000, midpoint);

      dc.SetPen(pen_peaks);
      dc.DrawLine(x, midpoint - peak_max, x, midpoint - peak_min);
      if (render_averages) {
        dc.SetPen(pen_avgs);
        dc.DrawLine(x, midpoint - avg_max, x, midpoint - avg_min);
      }
    }
	}

	// Horizontal zero-point line
	if (render_averages)
		dc.SetPen(wxPen(pal->get(1.0f)));
	else
		dc.SetPen(pen_peaks);

  for (int c = 0; c < channels; ++c)
  {
    int midpoint = (int)(rect.height / (float) channels * (c + 0.5f));
    dc.DrawLine(0, midpoint, rect.width, midpoint);
  }

	dc.SetPen(wxPen(pal->get(0.5f)));
  for (int c = 0; c < channels - 1; ++c)
  {
    // draw channel separators
    int blockWidth = (int)(rect.height / (float) channels) * c;
    dc.DrawLine(0, blockWidth, rect.width, blockWidth);
  }
}

void AudioWaveformRenderer::RenderBlank(wxDC &dc, const wxRect &rect, AudioRenderingStyle style)
{
	const AudioColorScheme *pal = &colors[style];
	wxColor line(pal->get(1.0));
	wxColor bg(pal->get(0.0));

	// Draw the line as background above and below, and line in the middle, to avoid
	// overdraw flicker (the common theme in all of audio display direct drawing).
	int halfheight = rect.height / 2;

	dc.SetBrush(wxBrush(bg));
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.DrawRectangle(rect.x, rect.y, rect.width, halfheight);
	dc.DrawRectangle(rect.x, rect.y + halfheight + 1, rect.width, rect.height - halfheight - 1);

	dc.SetPen(wxPen(line));
	dc.DrawLine(rect.x, rect.y+halfheight, rect.x+rect.width, rect.y+halfheight);
}

wxArrayString AudioWaveformRenderer::GetWaveformStyles() {
	wxArrayString ret;
	ret.push_back(_("Maximum"));
	ret.push_back(_("Maximum + Average"));
	return ret;
}
