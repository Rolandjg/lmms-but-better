/*
 * SlicerT.cpp - simple slicer plugin
 *
 * Copyright (c) 2023 Daniel Kauss Serna <daniel.kauss.serna@gmail.com>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "SlicerT.h"

#include <QDomElement>
#include <algorithm>
#include <cmath>
#include <fftw3.h>
#include <numbers>

#include "Engine.h"
#include "InstrumentTrack.h"
#include "PathUtil.h"
#include "SlicerTView.h"
#include "Song.h"
#include "embed.h"
#include "plugin_export.h"

namespace lmms {

extern "C" {
Plugin::Descriptor PLUGIN_EXPORT slicert_plugin_descriptor = {
	LMMS_STRINGIFY(PLUGIN_NAME),
	"SlicerT",
	QT_TRANSLATE_NOOP("PluginBrowser", "Basic Slicer"),
	"Daniel Kauss Serna <daniel.kauss.serna@gmail.com>",
	0x0100,
	Plugin::Type::Instrument,
	new PluginPixmapLoader("logo"),
	nullptr,
	nullptr,
};
} // end extern

namespace {

// Short fades that keep slice starts, note-offs and choked notes from clicking
constexpr float FadeInMs = 0.3f;
constexpr float ReleaseMs = 3.f;
constexpr float ChokeFadeMs = 4.f;
// One-shot notes keep playing after note-off until the slice ends, up to this long
constexpr float MaxOneShotMs = 60000.f;
// Slice boundaries, including the start and end of the sample.
// The slices and the whole sample key fill the keyboard.
constexpr std::size_t MaxSlicePoints = NumKeys;
// Grid slicing with snap set to "Off" uses 16th notes
constexpr int DefaultGridDivision = 16;

//! Equal-power fade gain at @p pos frames into a slice of @p length frames
float fadeGain(double pos, double length, float fadeIn, float fadeOut)
{
	constexpr float halfPi = std::numbers::pi_v<float> / 2.f;
	float gain = 1.f;
	if (fadeIn > 0.f && pos < fadeIn) { gain *= std::sin(std::clamp(static_cast<float>(pos) / fadeIn, 0.f, 1.f) * halfPi); }
	const auto untilEnd = static_cast<float>(length - pos);
	if (fadeOut > 0.f && untilEnd < fadeOut) { gain *= std::sin(std::clamp(untilEnd / fadeOut, 0.f, 1.f) * halfPi); }
	return gain;
}

//! Divisions per bar for a "Slice snap" index: Off, 1/1, 1/2, ... 1/32
int snapDivision(int snapIndex)
{
	return snapIndex > 0 ? 1 << (snapIndex - 1) : 0;
}

} // namespace

struct SlicerT::NoteState
{
	explicit NoteState(const Sample& sample)
		: slice(sample)
		, playback(AudioResampler::Mode::SincMedium)
	{
	}

	//! A copy of the sample bounded to this note's slice
	Sample slice;
	Sample::PlaybackState playback;
	std::uint64_t id = 0;
	//! Source frames played, tracked from the playback rate since the resampler reads ahead
	double position = 0.0;
	double length = 0.0;
	float start = 0.f;
	float end = 0.f;
	int chokeFramesDone = -1;
	bool finished = false;
};

// ################################# SlicerT ####################################

SlicerT::SlicerT(InstrumentTrack* instrumentTrack)
	: Instrument(instrumentTrack, &slicert_plugin_descriptor)
	, m_noteThreshold(0.6f, 0.0f, 2.0f, 0.01f, this, tr("Note threshold"))
	, m_fadeOutFrames(10.0f, 0.0f, 100.0f, 0.1f, this, tr("FadeOut"))
	, m_originalBPM(1, 1, 999, this, tr("Original bpm"))
	, m_sliceSnap(this, tr("Slice snap"))
	, m_enableSync(false, this, tr("BPM sync"))
	, m_sliceMode(this, tr("Slice mode"))
	, m_sliceCount(8, 2, static_cast<int>(MaxSlicePoints) - 1, this, tr("Slice count"))
	, m_choke(false, this, tr("Choke"))
	, m_oneShot(false, this, tr("One-shot"))
	, m_originalSample()
	, m_parentTrack(instrumentTrack)
{
	m_sliceSnap.addItem("Off");
	m_sliceSnap.addItem("1/1");
	m_sliceSnap.addItem("1/2");
	m_sliceSnap.addItem("1/4");
	m_sliceSnap.addItem("1/8");
	m_sliceSnap.addItem("1/16");
	m_sliceSnap.addItem("1/32");
	m_sliceSnap.setValue(0);

	m_sliceMode.addItem(tr("Transient"));
	m_sliceMode.addItem(tr("Grid"));
	m_sliceMode.addItem(tr("Equal"));
	m_sliceMode.setValue(static_cast<int>(SliceMode::Transients));

	connect(&m_sliceMode, &Model::dataChanged, this, &SlicerT::sliceSettingChanged);
	connect(&m_sliceCount, &Model::dataChanged, this, [this] {
		if (m_sliceMode.value() == static_cast<int>(SliceMode::Equal)) { sliceSettingChanged(); }
	});
	connect(&m_sliceSnap, &Model::dataChanged, this, [this] {
		if (m_sliceMode.value() != static_cast<int>(SliceMode::Equal)) { sliceSettingChanged(); }
	});
	// The tempo only moves slices when they are placed on a grid
	connect(&m_originalBPM, &Model::dataChanged, this, [this] {
		const auto mode = static_cast<SliceMode>(m_sliceMode.value());
		if (mode == SliceMode::Grid || (mode == SliceMode::Transients && m_sliceSnap.value() > 0))
		{
			sliceSettingChanged();
		}
	});
}

void SlicerT::playNote(NotePlayHandle* handle, SampleFrame* workingBuffer)
{
	const f_cnt_t frames = handle->framesLeftForCurrentPeriod();
	const f_cnt_t offset = handle->noteOffset();

	if (!handle->m_pluginData)
	{
		auto state = new NoteState(m_originalSample);
		handle->m_pluginData = state;

		const auto size = static_cast<int>(m_originalSample.sampleSize());
		float start, end;
		if (size > 1 && sliceBounds(handle->key(), start, end))
		{
			const auto startFrame = static_cast<int>(start * size);
			const auto endFrame = std::min(static_cast<int>(end * size), size);
			state->slice.setAllPointFrames(startFrame, endFrame, startFrame, endFrame);
			state->playback.setFrameIndex(startFrame);
			state->length = endFrame - startFrame;
			state->start = start;
			state->end = end;
			state->id = ++m_noteCounter;
		}
		state->finished = state->length <= 0;
	}

	auto state = static_cast<NoteState*>(handle->m_pluginData);
	const bool newest = state->id != 0 && state->id == m_noteCounter.load(std::memory_order_relaxed);

	if (state->finished)
	{
		zeroSampleFrames(workingBuffer, frames + offset);
		return;
	}

	SampleFrame* buf = workingBuffer + offset;
	const double ratio = playbackRatio();
	state->slice.play(buf, &state->playback, frames, Sample::Loop::Off, ratio);

	// Fades follow the output position in source frames
	const float sampleRate = std::max(1, m_originalSample.sampleRate());
	const double step = sampleRate / Engine::audioEngine()->outputSampleRate() / ratio;
	const float fadeIn = FadeInMs * sampleRate / 1000.f;
	const float fadeOut = m_fadeOutFrames.value() * sampleRate / 1000.f;
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		buf[f] *= fadeGain(state->position + f * step, state->length, fadeIn, fadeOut);
	}
	state->position += frames * step;

	// Choke: a newer note fades out the older ones
	if (m_choke.value() && !newest)
	{
		if (state->chokeFramesDone < 0) { state->chokeFramesDone = 0; }
		const auto chokeFrames = static_cast<int>(ChokeFadeMs * Engine::audioEngine()->outputSampleRate() / 1000.f);
		for (f_cnt_t f = 0; f < frames; ++f, ++state->chokeFramesDone)
		{
			buf[f] *= std::max(0.f, 1.f - static_cast<float>(state->chokeFramesDone) / chokeFrames);
		}
		if (state->chokeFramesDone >= chokeFrames) { state->finished = true; }
	}

	if (m_oneShot.value())
	{
		// One-shot notes ignore note-off; just avoid a click if the extended release runs out
		const auto releaseFrames = static_cast<f_cnt_t>(ReleaseMs * Engine::audioEngine()->outputSampleRate() / 1000.f);
		const auto endFrame = handle->framesLeft();
		for (auto f = endFrame - std::min(endFrame, releaseFrames); f < endFrame && f < frames + offset; ++f)
		{
			workingBuffer[f] *= static_cast<float>(endFrame - f) / releaseFrames;
		}
	}
	else { applyRelease(workingBuffer, handle); }

	if (state->position >= state->length) { state->finished = true; }

	if (newest)
	{
		if (state->finished) { emit isPlaying(-1, 0, 0); }
		else
		{
			const auto current = static_cast<float>(state->start + state->position / m_originalSample.sampleSize());
			emit isPlaying(current, state->start, state->end);
		}
	}
}

void SlicerT::deleteNotePluginData(NotePlayHandle* handle)
{
	auto state = static_cast<NoteState*>(handle->m_pluginData);
	if (state->id != 0 && state->id == m_noteCounter.load(std::memory_order_relaxed)) { emit isPlaying(-1, 0, 0); }
	delete state;
}

float SlicerT::desiredReleaseTimeMs() const
{
	if (!m_oneShot.value()) { return std::max(ReleaseMs, m_fadeOutFrames.value()); }

	// Long enough for the base note to play the whole sample at the current playback rate
	const float sampleMs = m_originalSample.sampleSize() * 1000.f / std::max(1, m_originalSample.sampleRate());
	return std::clamp(static_cast<float>(sampleMs * playbackRatio()), ReleaseMs, MaxOneShotMs);
}

f_cnt_t SlicerT::beatLen(NotePlayHandle* handle) const
{
	// Steps in the pattern editor play their whole slice
	float start, end;
	if (m_originalSample.sampleSize() <= 1 || !sliceBounds(handle->key(), start, end)) { return 0; }

	const double outputPerSource = static_cast<double>(Engine::audioEngine()->outputSampleRate())
		/ std::max(1, m_originalSample.sampleRate()) * playbackRatio();
	return static_cast<f_cnt_t>((end - start) * m_originalSample.sampleSize() * outputPerSource);
}

bool SlicerT::sliceBounds(int key, float& start, float& end) const
{
	const int index = key - firstSliceKey() + 1;
	if (index == 0) // full sample on the key below the first slice
	{
		start = 0;
		end = 1;
		return true;
	}
	if (index < 1 || index >= static_cast<int>(m_slicePoints.size())) { return false; }

	start = m_slicePoints[index - 1];
	end = m_slicePoints[index];
	// Points can be out of order while one is being dragged
	return end > start;
}

double SlicerT::playbackRatio() const
{
	const double pitchRatio = 1.0 / std::exp2(m_parentTrack->pitchModel()->value() / 1200.0);
	const double syncRatio = m_enableSync.value()
		? static_cast<double>(m_originalBPM.value()) / Engine::getSong()->getTempo()
		: 1.0;
	return syncRatio * pitchRatio;
}

// uses the spectral flux to determine the change in magnitude
// resources:
// http://www.iro.umontreal.ca/~pift6080/H09/documents/papers/bello_onset_tutorial.pdf
std::vector<float> SlicerT::transientFrames(int from, int to) const
{
	std::vector<float> slices;

	const int windowSize = 512;
	const float minBeatLength = 0.05f; // in seconds, ~ 1/4 length at 220 bpm

	int sampleRate = m_originalSample.sampleRate();
	int minDist = sampleRate * minBeatLength;

	float maxMag = 1E-10f;
	std::vector<float> singleChannel(m_originalSample.sampleSize(), 0);
	for (auto i = std::size_t{0}; i < m_originalSample.sampleSize(); i++)
	{
		singleChannel[i] = (m_originalSample.data()[i][0] + m_originalSample.data()[i][1]) / 2;
		maxMag = std::max(maxMag, std::abs(singleChannel[i]));
	}

	// normalize and find 0 crossings
	std::vector<int> zeroCrossings;
	float lastValue = 1;
	for (auto i = std::size_t{0}; i < singleChannel.size(); i++)
	{
		singleChannel[i] /= maxMag;
		if ((lastValue >= 0) != (singleChannel[i] >= 0))
		{
			zeroCrossings.push_back(i);
			lastValue = singleChannel[i];
		}
	}

	std::vector<float> prevMags(windowSize / 2, 0);
	std::vector<float> fftIn(windowSize, 0);
	std::array<fftwf_complex, windowSize> fftOut;

	fftwf_plan fftPlan = fftwf_plan_dft_r2c_1d(windowSize, fftIn.data(), fftOut.data(), FFTW_MEASURE);

	int lastPoint = from; // the range start is always a slice
	float spectralFlux = 0;
	float prevFlux = 1E-10f; // small value, no divison by zero

	for (int i = from; i < to - windowSize; i += windowSize)
	{
		// fft
		std::copy_n(singleChannel.data() + i, windowSize, fftIn.data());
		fftwf_execute(fftPlan);

		// calculate spectral flux in regard to last window
		for (int j = 0; j < windowSize / 2; j++) // only use niquistic frequencies
		{
			float real = fftOut[j][0];
			float imag = fftOut[j][1];
			float magnitude = std::sqrt(real * real + imag * imag);

			// using L1-norm (manhattan distance)
			float diff = std::abs(magnitude - prevMags[j]);
			spectralFlux += diff;

			prevMags[j] = magnitude;
		}

		if (spectralFlux / prevFlux > 1.0f + m_noteThreshold.value() && i - lastPoint > minDist)
		{
			slices.push_back(i);
			lastPoint = i;
			if (slices.size() >= MaxSlicePoints) { break; } // no more keys on the keyboard
		}

		prevFlux = spectralFlux;
		spectralFlux = 1E-10f; // again for no divison by zero
	}

	fftwf_destroy_plan(fftPlan);

	for (float& sliceValue : slices)
	{
		auto closestZeroCrossing = std::lower_bound(zeroCrossings.begin(), zeroCrossings.end(), sliceValue);
		if (closestZeroCrossing == zeroCrossings.end()) { continue; }
		if (std::abs(sliceValue - *closestZeroCrossing) < windowSize) { sliceValue = *closestZeroCrossing; }
	}

	if (const int division = snapDivision(m_sliceSnap.value()); division > 0)
	{
		const float samplesPerBar = m_originalSample.sampleRate() * 60.0f / m_originalBPM.value() * 4.0f;
		const int sliceLock = std::max(1, static_cast<int>(samplesPerBar / division));
		// The grid starts at the range start, so a range can begin on any downbeat
		for (float& sliceValue : slices)
		{
			sliceValue = from + std::round((sliceValue - from) / sliceLock) * sliceLock;
		}
	}

	return slices;
}

void SlicerT::findSlices()
{
	const auto size = m_originalSample.sampleSize();
	if (size <= 1) { return; }

	// Only the slice range is sliced again, the slices around it are kept
	const auto from = static_cast<int>(m_sliceRangeStart * size);
	const auto to = static_cast<int>(m_sliceRangeEnd * size);

	std::vector<float> frames;
	switch (static_cast<SliceMode>(m_sliceMode.value()))
	{
	case SliceMode::Transients:
		frames = transientFrames(from, to);
		break;
	case SliceMode::Grid:
	{
		const int snap = snapDivision(m_sliceSnap.value());
		const double samplesPerBar = m_originalSample.sampleRate() * 60.0 / m_originalBPM.value() * 4.0;
		const double step = samplesPerBar / (snap > 0 ? snap : DefaultGridDivision);
		// Skip a last slice shorter than a quarter of the grid
		for (double frame = from + step; frame < to - step * 0.25 && frames.size() < MaxSlicePoints; frame += step)
		{
			frames.push_back(frame);
		}
		break;
	}
	case SliceMode::Equal:
	{
		const double step = static_cast<double>(to - from) / m_sliceCount.value();
		for (int i = 1; i < m_sliceCount.value(); ++i) { frames.push_back(from + i * step); }
		break;
	}
	}

	std::sort(frames.begin(), frames.end());

	const float start = static_cast<float>(from) / size;
	const float end = static_cast<float>(to) / size;
	const float minGap = 1.f / size;

	std::vector<float> after;
	for (float point : m_slicePoints)
	{
		if (point > end + minGap && point < 1.f) { after.push_back(point); }
	}

	std::vector<float> slicePoints{0.f};
	const auto add = [&](float point) {
		if (point - slicePoints.back() >= minGap) { slicePoints.push_back(point); }
	};
	for (float point : m_slicePoints)
	{
		if (point < start - minGap) { add(point); }
	}
	add(start);
	for (float frame : frames)
	{
		const float point = frame / size;
		// Leave room for the kept slices after the range and the sample end
		if (point < end - minGap && slicePoints.size() + after.size() < MaxSlicePoints - 2) { add(point); }
	}
	if (end < 1.f) { add(end); }
	for (float point : after) { add(point); }
	if (slicePoints.back() > 1.f - minGap) { slicePoints.back() = 1.f; }
	else { slicePoints.push_back(1.f); }

	setSlicePoints(std::move(slicePoints));
}

// find the bpm of the sample by assuming its in 4/4 time signature ,
// and lies in the 100 - 200 bpm range
void SlicerT::findBPM()
{
	if (m_originalSample.sampleSize() <= 1) { return; }

	float sampleRate = m_originalSample.sampleRate();
	float totalFrames = m_originalSample.sampleSize();
	float sampleLength = totalFrames / sampleRate;

	float bpmEstimate = 240.0f / sampleLength;

	while (bpmEstimate < 100)
	{
		bpmEstimate *= 2;
	}

	while (bpmEstimate > 200)
	{
		bpmEstimate /= 2;
	}

	m_originalBPM.setValue(bpmEstimate);
	m_originalBPM.setInitValue(bpmEstimate);
}

std::vector<Note> SlicerT::getMidi()
{
	std::vector<Note> outputNotes;

	float speedRatio = static_cast<float>(m_originalBPM.value()) / Engine::getSong()->getTempo();
	float outFrames = m_originalSample.sampleSize() * speedRatio;

	float framesPerTick = Engine::framesPerTick();
	float totalTicks = outFrames / framesPerTick;
	float lastEnd = 0;

	for (auto i = std::size_t{0}; i + 1 < m_slicePoints.size(); i++)
	{
		float sliceStart = lastEnd;
		float sliceEnd = totalTicks * m_slicePoints[i + 1];

		Note sliceNote = Note();
		sliceNote.setKey(firstSliceKey() + static_cast<int>(i));
		sliceNote.setPos(sliceStart);
		sliceNote.setLength(sliceEnd - sliceStart + 1); // + 1 so that the notes allign
		outputNotes.push_back(sliceNote);

		lastEnd = sliceEnd;
	}

	return outputNotes;
}

void SlicerT::setSlicePoints(std::vector<float> slicePoints)
{
	Engine::audioEngine()->requestChangeInModel();
	m_slicePoints = std::move(slicePoints);
	Engine::audioEngine()->doneChangeInModel();

	if (!m_suppressReslice) { Engine::getSong()->setModified(); }
	emit dataChanged();
}

void SlicerT::addSlicePoint(float position)
{
	if (m_slicePoints.size() >= MaxSlicePoints) { return; }
	position = std::clamp(position, 0.f, 1.f);

	Engine::audioEngine()->requestChangeInModel();
	m_slicePoints.insert(std::upper_bound(m_slicePoints.begin(), m_slicePoints.end(), position), position);
	Engine::audioEngine()->doneChangeInModel();

	Engine::getSong()->setModified();
}

void SlicerT::removeSlicePoint(std::size_t index)
{
	if (m_slicePoints.size() <= 2 || index >= m_slicePoints.size()) { return; }

	Engine::audioEngine()->requestChangeInModel();
	m_slicePoints.erase(m_slicePoints.begin() + index);
	Engine::audioEngine()->doneChangeInModel();

	Engine::getSong()->setModified();
}

void SlicerT::moveSlicePoint(std::size_t index, float position)
{
	if (index >= m_slicePoints.size()) { return; }

	Engine::audioEngine()->requestChangeInModel();
	m_slicePoints[index] = std::clamp(position, 0.f, 1.f);
	Engine::audioEngine()->doneChangeInModel();
}

void SlicerT::finishSliceEdit()
{
	Engine::audioEngine()->requestChangeInModel();
	std::sort(m_slicePoints.begin(), m_slicePoints.end());
	Engine::audioEngine()->doneChangeInModel();

	Engine::getSong()->setModified();
}

void SlicerT::clearSlices()
{
	// Points are kept at the start (0) and end (1) of the sample,
	// so the whole sample can still be copied using MIDI.
	setSlicePoints({0.f, 1.f});
}

void SlicerT::setSliceRange(float start, float end)
{
	m_sliceRangeStart = std::clamp(start, 0.f, 1.f);
	m_sliceRangeEnd = std::clamp(end, m_sliceRangeStart, 1.f);
}

int SlicerT::sliceAt(float position) const
{
	for (auto i = std::size_t{0}; i + 1 < m_slicePoints.size(); i++)
	{
		if (position >= m_slicePoints[i] && position < m_slicePoints[i + 1]) { return static_cast<int>(i); }
	}
	return -1;
}

int SlicerT::firstSliceKey() const
{
	// Slices start above the base note, and move down when there are more than fit above it
	const auto slices = static_cast<int>(m_slicePoints.size()) - 1;
	return std::clamp(m_parentTrack->baseNote() + 1, 1, std::max(1, NumKeys - slices));
}

void SlicerT::sliceSettingChanged()
{
	if (m_suppressReslice) { return; }
	findSlices();
}

void SlicerT::updateFile(QString file)
{
	if (auto buffer = SampleBuffer::fromFile(file))
	{
		Engine::audioEngine()->requestChangeInModel();
		m_originalSample = Sample(std::move(buffer));
		Engine::audioEngine()->doneChangeInModel();
	}

	// A new sample is sliced as a whole
	setSliceRange(0.f, 1.f);

	// findSlices() below picks up the new tempo
	m_suppressReslice = true;
	findBPM();
	m_suppressReslice = false;

	findSlices();

	emit dataChanged();
}

void SlicerT::loadFile(const QString& file)
{
	updateFile(file);
}

void SlicerT::updateSlices()
{
	findSlices();
}

void SlicerT::saveSettings(QDomDocument& document, QDomElement& element)
{
	element.setAttribute("version", "1");
	element.setAttribute("src", m_originalSample.sampleFile());
	if (m_originalSample.sampleFile().isEmpty())
	{
		element.setAttribute("sampledata", m_originalSample.toBase64());
	}

	element.setAttribute("rangeStart", m_sliceRangeStart);
	element.setAttribute("rangeEnd", m_sliceRangeEnd);

	element.setAttribute("totalSlices", static_cast<int>(m_slicePoints.size()));
	for (auto i = std::size_t{0}; i < m_slicePoints.size(); i++)
	{
		element.setAttribute(QString("slice_%1").arg(i), m_slicePoints[i]);
	}

	m_fadeOutFrames.saveSettings(document, element, "fadeOut");
	m_noteThreshold.saveSettings(document, element, "threshold");
	m_originalBPM.saveSettings(document, element, "origBPM");
	m_enableSync.saveSettings(document, element, "syncEnable");
	m_sliceSnap.saveSettings(document, element, "sliceSnap");
	m_sliceMode.saveSettings(document, element, "sliceMode");
	m_sliceCount.saveSettings(document, element, "sliceCount");
	m_choke.saveSettings(document, element, "choke");
	m_oneShot.saveSettings(document, element, "oneShot");
}

void SlicerT::loadSettings(const QDomElement& element)
{
	m_suppressReslice = true;

	std::shared_ptr<const SampleBuffer> buffer;
	if (auto srcFile = element.attribute("src"); !srcFile.isEmpty())
	{
		if (QFileInfo(PathUtil::toAbsolute(srcFile)).exists()) { buffer = SampleBuffer::fromFile(srcFile); }
		else
		{
			QString message = tr("Sample not found: %1").arg(srcFile);
			Engine::getSong()->collectError(message);
		}
	}
	else if (auto sampleData = element.attribute("sampledata"); !sampleData.isEmpty())
	{
		buffer = SampleBuffer::fromBase64(sampleData);
	}

	if (buffer)
	{
		Engine::audioEngine()->requestChangeInModel();
		m_originalSample = Sample(std::move(buffer));
		Engine::audioEngine()->doneChangeInModel();
	}

	// Settings added later fall back to their defaults in older projects
	const auto load = [&element](AutomatableModel& model, const QString& name, float fallback) {
		if (element.hasAttribute(name) || !element.firstChildElement(name).isNull()) { model.loadSettings(element, name); }
		else { model.setValue(fallback); }
	};

	m_fadeOutFrames.loadSettings(element, "fadeOut");
	m_noteThreshold.loadSettings(element, "threshold");
	m_originalBPM.loadSettings(element, "origBPM");
	m_enableSync.loadSettings(element, "syncEnable");
	load(m_sliceSnap, "sliceSnap", 0);
	load(m_sliceMode, "sliceMode", static_cast<int>(SliceMode::Transients));
	load(m_sliceCount, "sliceCount", 8);
	load(m_choke, "choke", 0);
	load(m_oneShot, "oneShot", 0);

	setSliceRange(element.attribute("rangeStart", "0").toFloat(), element.attribute("rangeEnd", "1").toFloat());

	if (!element.attribute("totalSlices").isEmpty())
	{
		int totalSlices = element.attribute("totalSlices").toInt();
		std::vector<float> slicePoints;
		for (int i = 0; i < totalSlices; i++)
		{
			slicePoints.push_back(element.attribute(QString("slice_%1").arg(i)).toFloat());
		}
		setSlicePoints(std::move(slicePoints));
	}

	m_suppressReslice = false;

	emit dataChanged();
}

QString SlicerT::nodeName() const
{
	return slicert_plugin_descriptor.name;
}

gui::PluginView* SlicerT::instantiateView(QWidget* parent)
{
	return new gui::SlicerTView(this, parent);
}

extern "C" {
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* m, void*)
{
	return new SlicerT(static_cast<InstrumentTrack*>(m));
}
} // extern
} // namespace lmms
