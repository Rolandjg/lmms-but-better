/* Copyright (c) 2026 LMMS developers. SPDX-License-Identifier: GPL-2.0-or-later */
#include <QtTest>
#include <cmath>
#include <numbers>

#include "../../../plugins/LoudnessMeter/LoudnessAnalyzer.h"

using lmms::LoudnessAnalyzer;

class LoudnessAnalyzerTest : public QObject
{
	Q_OBJECT

	static constexpr float Rate = 48000.f;

	//! Stereo 1 kHz sine, level in dBFS as in EBU Tech 3341 (a full-scale sine is 0 dBFS)
	static void feedSine(LoudnessAnalyzer& a, float dbfs, float seconds, double& phase)
	{
		const double amplitude = std::pow(10.0, dbfs / 20.0);
		const double step = 2.0 * std::numbers::pi * 997.0 / Rate;
		for (int i = 0; i < static_cast<int>(seconds * Rate); ++i)
		{
			const auto s = static_cast<float>(amplitude * std::sin(phase));
			phase += step;
			a.process(s, s);
		}
	}

private slots:
	// EBU Tech 3341 test signal 1: stereo 1 kHz at -23 dBFS reads -23 LUFS
	void referenceLevel()
	{
		LoudnessAnalyzer a;
		a.setSampleRate(Rate);
		double phase = 0;
		feedSine(a, -23.f, 20.f, phase);
		QVERIFY2(std::abs(a.integrated() - -23.f) < 0.1f, qPrintable(QString::number(a.integrated())));
		QVERIFY(std::abs(a.momentary() - -23.f) < 0.1f);
		QVERIFY(std::abs(a.shortTerm() - -23.f) < 0.1f);
	}

	// Silence is gated out of the integrated value
	void gating()
	{
		LoudnessAnalyzer a;
		a.setSampleRate(Rate);
		double phase = 0;
		feedSine(a, -20.f, 10.f, phase);
		for (int i = 0; i < static_cast<int>(10 * Rate); ++i) { a.process(0.f, 0.f); }
		QVERIFY2(std::abs(a.integrated() - -20.f) < 0.1f, qPrintable(QString::number(a.integrated())));
		QVERIFY(!std::isfinite(a.momentary()));
	}

	// EBU Tech 3342 case 1: 20 s at -20 then 20 s at -30 has a range of 10 LU
	void loudnessRange()
	{
		LoudnessAnalyzer a;
		a.setSampleRate(Rate);
		double phase = 0;
		feedSine(a, -20.f, 20.f, phase);
		feedSine(a, -30.f, 20.f, phase);
		QVERIFY2(std::abs(a.loudnessRange() - 10.f) < 1.f, qPrintable(QString::number(a.loudnessRange())));
	}

	// Inter-sample peaks: fs/4 sine at 45 degrees never hits its peak on a sample
	void truePeak()
	{
		LoudnessAnalyzer a;
		a.setSampleRate(Rate);
		float samplePeak = 0.f;
		for (int i = 0; i < 4800; ++i)
		{
			const auto s = static_cast<float>(0.5 * std::sin(std::numbers::pi / 2 * i + std::numbers::pi / 4));
			samplePeak = std::max(samplePeak, std::abs(s));
			a.process(s, s);
		}
		QVERIFY(samplePeak < 0.36f);
		const float tpDb = 20.f * std::log10(a.truePeak());
		QVERIFY2(std::abs(tpDb - 20.f * std::log10(0.5f)) < 0.3f, qPrintable(QString::number(tpDb)));
	}

	void reset()
	{
		LoudnessAnalyzer a;
		a.setSampleRate(Rate);
		double phase = 0;
		feedSine(a, -10.f, 2.f, phase);
		a.reset();
		QVERIFY(!std::isfinite(a.integrated()));
		QCOMPARE(a.truePeak(), 0.f);
	}
};

QTEST_GUILESS_MAIN(LoudnessAnalyzerTest)
#include "LoudnessAnalyzerTest.moc"
