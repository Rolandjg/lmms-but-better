/* Copyright (c) 2026 LMMS developers. SPDX-License-Identifier: GPL-2.0-or-later */
#include <QtTest>
#include <cmath>
#include <numbers>
#include <random>

#include "../../../plugins/FormantShifter/FormantDsp.h"

using namespace lmms::formant;

class FormantDspTest : public QObject
{
	Q_OBJECT

	static constexpr float Rate = 48000.f;

	//! A sung "A" stand-in: a 110 Hz sawtooth through the tenor vowel filter
	static std::vector<float> vowelSource(std::size_t length, int vowel = 0)
	{
		ResonantVowelFilter filter;
		filter.setSampleRate(Rate);
		filter.set(vowelTable(1, vowel), 1.f);
		std::vector<float> out(length);
		double phase = 0.0;
		for (auto& s : out)
		{
			phase += 110.0 / Rate;
			phase -= std::floor(phase);
			s = 0.3f * filter.process(static_cast<float>(2.0 * phase - 1.0));
		}
		return out;
	}

	static std::vector<float> run(SpectralEngine& engine, const std::vector<float>& in)
	{
		std::vector<float> out(in.size());
		for (std::size_t i = 0; i < in.size(); ++i) { out[i] = engine.process(in[i]); }
		return out;
	}

	static double rms(const std::vector<float>& x, std::size_t from, std::size_t to)
	{
		double sum = 0.0;
		for (std::size_t i = from; i < to; ++i) { sum += x[i] * x[i]; }
		return std::sqrt(sum / (to - from));
	}

	//! Frequency of the strongest component between lo and hi (Hann-windowed DFT scan)
	static double dominantFrequency(const std::vector<float>& x, std::size_t start, int length, double lo, double hi)
	{
		double best = 0.0, bestFreq = 0.0;
		for (double f = lo; f < hi; f += 0.25)
		{
			double re = 0.0, im = 0.0;
			for (int i = 0; i < length; ++i)
			{
				const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / length);
				re += x[start + i] * w * std::cos(2.0 * std::numbers::pi * f * i / Rate);
				im += x[start + i] * w * std::sin(2.0 * std::numbers::pi * f * i / Rate);
			}
			if (re * re + im * im > best) { best = re * re + im * im; bestFreq = f; }
		}
		return bestFreq;
	}

	//! Frequency of the highest point of an engine envelope in the formant range
	static float envelopePeak(const SpectralEngine& engine, const std::vector<float>& envelope)
	{
		const float binHz = Rate / engine.size();
		int best = static_cast<int>(300.f / binHz);
		for (int k = best; k < static_cast<int>(1500.f / binHz); ++k) { if (envelope[k] > envelope[best]) { best = k; } }
		return best * binHz;
	}

private slots:
	void neutralSettingsPassAudioThroughDelayedByTheLatency()
	{
		SpectralEngine engine;
		engine.setSampleRate(Rate);
		QCOMPARE(engine.latency(), 2048);
		std::mt19937 rng(7);
		std::normal_distribution<float> noise(0.f, 0.2f);
		std::vector<float> in(24000);
		for (auto& s : in) { s = noise(rng); }
		const auto out = run(engine, in);
		const auto latency = static_cast<std::size_t>(engine.latency());
		for (std::size_t i = latency + 4096; i < in.size(); ++i)
		{
			QVERIFY(std::abs(out[i] - in[i - latency]) < 1e-4f);
		}
	}

	void largerFrameAtHighSampleRates()
	{
		SpectralEngine engine;
		engine.setSampleRate(96000.f);
		QCOMPARE(engine.size(), 4096);
		QCOMPARE(engine.latency(), 4096);
	}

	void pitchShiftHitsTheTargetFrequency()
	{
		for (float semitones : {7.f, -5.f, 12.f, 0.37f})
		{
			SpectralEngine engine;
			engine.setSampleRate(Rate);
			SpectralEngine::Settings settings;
			settings.pitchRatio = std::exp2(semitones / 12.f);
			settings.formantRatio = settings.pitchRatio;
			engine.setSettings(settings);
			std::vector<float> in(36000);
			for (std::size_t i = 0; i < in.size(); ++i) { in[i] = 0.3f * std::sin(2.f * std::numbers::pi_v<float> * 220.f * i / Rate); }
			const auto out = run(engine, in);
			const double expected = 220.0 * settings.pitchRatio;
			const double measured = dominantFrequency(out, 20000, 8192, expected * 0.8, expected * 1.2);
			QVERIFY2(std::abs(measured - expected) < 0.5, qPrintable(QString("%1 st: %2 Hz").arg(semitones).arg(measured)));
			QVERIFY(rms(out, 20000, 34000) > 0.15); // level kept when the formants follow
		}
	}

	void formantShiftMovesTheEnvelopeButNotThePitch()
	{
		const auto in = vowelSource(48000);
		for (float semitones : {4.f, -4.f})
		{
			SpectralEngine engine;
			engine.setSampleRate(Rate);
			SpectralEngine::Settings settings;
			settings.formantRatio = std::exp2(semitones / 12.f);
			engine.setSettings(settings);
			const auto out = run(engine, in);
			const float before = envelopePeak(engine, engine.inputEnvelope());
			const float after = envelopePeak(engine, engine.outputEnvelope());
			QVERIFY2(std::abs(after / before - settings.formantRatio) < 0.06f,
				qPrintable(QString("%1 -> %2 Hz").arg(before).arg(after)));
			QCOMPARE(dominantFrequency(out, 30000, 16384, 100, 120), 110.0);
			QVERIFY(std::abs(20.0 * std::log10(rms(out, 10000, 46000) / rms(in, 10000, 46000))) < 3.0);
		}
	}

	void keepingFormantsWhilePitchShifting()
	{
		const auto in = vowelSource(48000);
		SpectralEngine engine;
		engine.setSampleRate(Rate);
		SpectralEngine::Settings settings;
		settings.pitchRatio = std::exp2(7.f / 12.f);
		engine.setSettings(settings);
		run(engine, in);
		const float before = envelopePeak(engine, engine.inputEnvelope());
		const float after = envelopePeak(engine, engine.outputEnvelope());
		QVERIFY(std::abs(after / before - 1.f) < 0.06f);
	}

	void imprintReplacesTheFormantsAtAboutTheInputLevel()
	{
		// A talkbox is fed a buzzy synth: a plain sawtooth
		std::vector<float> in(48000);
		double phase = 0.0;
		for (auto& s : in)
		{
			phase += 110.0 / Rate;
			phase -= std::floor(phase);
			s = 0.2f * static_cast<float>(2.0 * phase - 1.0);
		}
		SpectralEngine engine;
		engine.setSampleRate(Rate);
		SpectralEngine::Settings settings;
		settings.imprint = 1.f;
		settings.vowel = vowelTable(1, 2); // imprint an "I": F1 290 Hz, F2 1870 Hz
		engine.setSettings(settings);
		const auto out = run(engine, in);
		const auto& env = engine.outputEnvelope();
		const auto at = [&](float hz) { return env[static_cast<int>(hz * engine.size() / Rate)] * 20.f / std::log(10.f); };
		QVERIFY2(at(290.f) > at(650.f) + 10.f, qPrintable(QString("%1 %2").arg(at(290.f)).arg(at(650.f))));
		QVERIFY2(at(1870.f) > at(1300.f) + 10.f, qPrintable(QString("%1 %2").arg(at(1870.f)).arg(at(1300.f))));
		QVERIFY(std::abs(20.0 * std::log10(rms(out, 10000, 46000) / rms(in, 10000, 46000))) < 4.0);
	}

	void resonantFilterIsStableAndLevelled()
	{
		for (int vowel = 0; vowel < VowelCount; ++vowel)
		{
			ResonantVowelFilter filter;
			filter.setSampleRate(Rate);
			double phase = 0.0, inSum = 0.0, outSum = 0.0;
			for (int i = 0; i < 96000; ++i)
			{
				// Sweep the vowel and the formants while running, as automation would
				if (i % 32 == 0)
				{
					const float t = i / 96000.f;
					filter.set(scaledVowel(vowelAt(vowel % 2 ? 0 : 4, t * 4.f), std::exp2((t - 0.5f) * 2.f), 1.f), 0.25f + t * 3.75f);
				}
				phase += 110.0 / Rate;
				phase -= std::floor(phase);
				const float x = 0.3f * static_cast<float>(2.0 * phase - 1.0);
				const float y = filter.process(x);
				QVERIFY(std::isfinite(y) && std::abs(y) < 10.f);
				inSum += x * x;
				outSum += y * y;
			}
			QVERIFY(std::abs(10.0 * std::log10(outSum / inSum)) < 9.0);
		}
	}

	void vowelMorphInterpolatesBetweenTableEntries()
	{
		const auto a = vowelTable(1, 0);
		const auto e = vowelTable(1, 1);
		QCOMPARE(vowelAt(1, 0.f)[0].freq, a[0].freq);
		QCOMPARE(vowelAt(1, 1.f)[1].freq, e[1].freq);
		const float mid = vowelAt(1, 0.5f)[0].freq;
		QVERIFY(std::abs(mid - std::sqrt(a[0].freq * e[0].freq)) < 0.5f);
		QCOMPARE(vowelAt(1, 9.f)[0].freq, vowelTable(1, 4)[0].freq);
	}

	void lfoShapesAreBipolarAndRandomOnesRepeat()
	{
		for (int shape = 0; shape <= static_cast<int>(LfoShape::SmoothRandom); ++shape)
		{
			for (double phase = 0.0; phase < 8.0; phase += 0.013)
			{
				const float v = lfoValue(static_cast<LfoShape>(shape), phase);
				QVERIFY(v >= -1.0001f && v <= 1.0001f);
			}
		}
		QCOMPARE(lfoValue(LfoShape::SampleHold, 5.2), lfoValue(LfoShape::SampleHold, 5.9));
		QVERIFY(lfoValue(LfoShape::SampleHold, 5.2) != lfoValue(LfoShape::SampleHold, 6.2));
		QCOMPARE(lfoValue(LfoShape::Triangle, 0.25), 1.f);
	}
};

QTEST_GUILESS_MAIN(FormantDspTest)
#include "FormantDspTest.moc"
