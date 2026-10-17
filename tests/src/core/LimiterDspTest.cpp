/* Copyright (c) 2026 LMMS developers. SPDX-License-Identifier: GPL-2.0-or-later */
#include <QtTest>
#include <cmath>
#include <numbers>
#include <random>

#include "../../../plugins/Limiter/LimiterDsp.h"

using lmms::LimiterDsp;

class LimiterDspTest : public QObject
{
	Q_OBJECT

	static constexpr float Rate = 48000.f;

private slots:
	void transparentBelowCeiling()
	{
		LimiterDsp dsp;
		dsp.setSampleRate(Rate);
		dsp.setCeiling(1.f);
		dsp.setLookahead(2.f);
		const int latency = dsp.latency();
		std::vector<float> in(4000);
		for (std::size_t i = 0; i < in.size(); ++i) { in[i] = 0.5f * std::sin(i * 0.05f); }
		for (std::size_t i = 0; i < in.size(); ++i)
		{
			float l = in[i], r = -in[i];
			const float gain = dsp.process(l, r);
			QCOMPARE(gain, 1.f);
			const float expected = i >= static_cast<std::size_t>(latency) ? in[i - latency] : 0.f;
			QVERIFY(std::abs(l - expected) < 1e-6f);
			QVERIFY(std::abs(r + expected) < 1e-6f);
		}
	}

	void neverExceedsCeiling()
	{
		for (bool truePeak : {false, true})
		{
			for (float lookahead : {0.1f, 1.f, 5.f})
			{
				LimiterDsp dsp;
				dsp.setSampleRate(Rate);
				dsp.setCeiling(0.5f);
				dsp.setInputGain(4.f);
				dsp.setRelease(50.f);
				dsp.setLookahead(lookahead);
				dsp.setStereoLink(0.5f);
				dsp.setTruePeak(truePeak);
				std::mt19937 rng(1234);
				std::normal_distribution<float> noise(0.f, 0.3f);
				for (int i = 0; i < 48000; ++i)
				{
					// Bursts of noise and transients over a quiet bed
					const float env = (i / 4800) % 2 ? 1.f : 0.05f;
					float l = noise(rng) * env;
					float r = (i % 997 == 0 ? 1.f : noise(rng)) * env;
					dsp.process(l, r);
					QVERIFY2(std::abs(l) <= 0.5f + 1e-6f && std::abs(r) <= 0.5f + 1e-6f, "ceiling exceeded");
				}
			}
		}
	}

	void reductionArrivesWithThePeak()
	{
		// A single loud spike must already be fully reduced when it comes out, without the clamp having to act
		LimiterDsp dsp;
		dsp.setSampleRate(Rate);
		dsp.setCeiling(0.5f);
		dsp.setLookahead(1.f);
		dsp.setTruePeak(false);
		const int latency = dsp.latency();
		float outPeak = 0.f;
		for (int i = 0; i < 2000; ++i)
		{
			float l = i == 100 ? 1.f : 0.1f;
			float r = l;
			const float gain = dsp.process(l, r);
			if (i == 100 + latency)
			{
				QVERIFY(gain <= 0.5f + 1e-5f);
				outPeak = l;
			}
		}
		QVERIFY(std::abs(outPeak - 0.5f) < 1e-3f);
	}

	void truePeakCatchesIntersamplePeaks()
	{
		// A sine at a quarter of the sample rate, sampled 45 degrees off its peaks, has sample peaks 3 dB
		// below its true peak
		const auto run = [](bool truePeak) {
			LimiterDsp dsp;
			dsp.setSampleRate(Rate);
			dsp.setCeiling(0.9f);
			dsp.setTruePeak(truePeak);
			float minGain = 1.f;
			for (int i = 0; i < 4800; ++i)
			{
				float l = std::sin(std::numbers::pi_v<float> / 2.f * i + std::numbers::pi_v<float> / 4.f);
				float r = l;
				minGain = std::min(minGain, dsp.process(l, r));
			}
			return minGain;
		};
		// Sample peaks are at 0.707, under the 0.9 ceiling; the true peak of 1.0 is above it
		QCOMPARE(run(false), 1.f);
		QVERIFY(run(true) < 0.92f);
	}
};

QTEST_GUILESS_MAIN(LimiterDspTest)
#include "LimiterDspTest.moc"
