/*
 * FunctionGenerator.h - waveform math behind the automation "generate shape" tool
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
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef LMMS_FUNCTION_GENERATOR_H
#define LMMS_FUNCTION_GENERATOR_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "FunctionExpression.h"

namespace lmms
{

//! Parameters of a periodic function laid over a stretch of automation.
//! The function is evaluated in a normalised 0..1 range and mapped to [low, high] at the end.
struct FunctionGenerator
{
	enum class Shape
	{
		Sine,
		Triangle,
		SawUp,
		SawDown,
		Square,
		Noise,       //!< stepped random (sample & hold)
		SmoothNoise, //!< random values joined by smooth curves
		Custom,      //!< the math expression
	};

	//! A piecewise linear graph over normalised time. x and y are both 0..1.
	//! An empty curve is "off".
	struct Curve
	{
		std::vector<std::pair<double, double>> points;

		bool empty() const { return points.empty(); }
		bool flat() const
		{
			return std::all_of(points.begin(), points.end(), [&](const auto& pt) { return pt.second == points[0].second; });
		}

		double at(double x) const
		{
			if (points.empty()) { return 1.0; }
			if (x <= points.front().first) { return points.front().second; }
			if (x >= points.back().first) { return points.back().second; }
			const auto hi = std::upper_bound(points.begin(), points.end(), x,
				[](double v, const auto& pt) { return v < pt.first; });
			const auto lo = hi - 1;
			const double span = hi->first - lo->first;
			return span <= 0.0 ? hi->second : lo->second + (hi->second - lo->second) * (x - lo->first) / span;
		}

		//! Sorts the points and clamps them into the unit square
		void normalise()
		{
			for (auto& pt : points)
			{
				pt.first = std::clamp(pt.first, 0.0, 1.0);
				pt.second = std::clamp(pt.second, 0.0, 1.0);
			}
			std::stable_sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		}

		bool operator==(const Curve&) const = default;
	};

	//! One automation node; in != out makes a jump
	struct Node
	{
		double tick;
		float in;
		float out;
	};

	Shape shape = Shape::Sine;
	double cycles = 4.0;     //!< cycles over the generated length
	double phase = 0.0;      //!< start phase in cycles (0..1)
	double symmetry = 0.5;   //!< where the cycle turns: skew for triangle/sine, duty cycle for square
	double bend = 0.0;       //!< -1..1, curves the output towards low or high
	int steps = 0;           //!< quantise to this many levels (0 or 1 = off)
	int cells = 8;           //!< random values per cycle for the noise shapes
	int detail = 32;         //!< samples per cycle used for curved parts
	unsigned seed = 1;       //!< noise seed
	bool invert = false;
	float low = 0.f;
	float high = 1.f;

	//! Scales the swing around the middle of the range over time (y: 0 = flat, 1 = full)
	Curve amplitude;
	//! Speeds the cycles up or down over time (y: 0.5 = unchanged). The total number of cycles stays `cycles`.
	Curve frequency;
	double frequencyRange = 2.0; //!< octaves the frequency curve spans in each direction

	//! Used by Shape::Custom. Variables: p (position in cycles), t (0..1 over the stretch), n (cycle number);
	//! the result is expected in -1..1. See FunctionExpression.h for the available functions.
	std::string expression = "sin(tau * p)";

	static constexpr int MaxNodes = 8192;

	bool usesSymmetry() const
	{
		return shape == Shape::Sine || shape == Shape::Triangle || shape == Shape::Square;
	}
	bool usesCells() const { return shape == Shape::Noise || shape == Shape::SmoothNoise; }
	bool usesBend() const { return shape != Shape::Square && shape != Shape::Noise; }
	bool amplitudeActive() const { return !amplitude.empty() && !(amplitude.flat() && amplitude.points[0].second == 1.0); }
	bool frequencyActive() const { return !frequency.empty() && !frequency.flat(); }

	//! Empty if the expression compiles, otherwise what is wrong with it
	std::string expressionError() const
	{
		compiled();
		return m_error;
	}

	//! Normalised (0..1) value at a position given in cycles
	double valueAt(double p) const
	{
		const Warp warp(*this);
		return sample(p, warp);
	}

	//! Value mapped to the output range
	float outputAt(double p) const
	{
		return low + static_cast<float>(valueAt(p)) * (high - low);
	}

	//! Builds the node list for a stretch of `lengthTicks` ticks. Straight parts only get
	//! nodes at their ends; curved or quantised parts are sampled `detail` times per cycle.
	std::vector<Node> build(double lengthTicks) const
	{
		std::vector<Node> out;
		if (lengthTicks <= 0.0 || cycles <= 0.0) { return out; }

		const Warp warp(*this);
		const double pStart = phase;
		const double pEnd = phase + cycles;
		const auto toTick = [&](double p) { return warp.timeAt(p) * lengthTicks; };
		constexpr double eps = 1e-7;

		// positions inside one cycle where the function has a corner or a jump
		std::vector<double> corners;
		switch (shape)
		{
			case Shape::Triangle: corners = {0.0, std::clamp(symmetry, 0.01, 0.99)}; break;
			case Shape::SawUp:
			case Shape::SawDown: corners = {0.0}; break;
			case Shape::Square: corners = {0.0, std::clamp(symmetry, 0.01, 0.99)}; break;
			case Shape::Noise:
			case Shape::SmoothNoise:
				for (int i = 0; i < cells; ++i) { corners.push_back(static_cast<double>(i) / cells); }
				break;
			case Shape::Sine:
			case Shape::Custom: corners = {0.0}; break;
		}

		const bool quantised = steps > 1;
		const bool ramps = shape == Shape::Triangle || shape == Shape::SawUp || shape == Shape::SawDown;
		const bool dense = shape == Shape::Sine || shape == Shape::SmoothNoise || shape == Shape::Custom
			|| (ramps && (bend != 0.0 || quantised || amplitudeActive()));

		// breakpoint list in phase space, including both ends
		std::vector<double> points{pStart};
		for (long n = static_cast<long>(std::floor(pStart)); n <= static_cast<long>(std::ceil(pEnd)); ++n)
		{
			for (double c : corners)
			{
				const double p = n + c;
				if (p > pStart + eps && p < pEnd - eps) { points.push_back(p); }
			}
		}
		points.push_back(pEnd);
		std::sort(points.begin(), points.end());
		points.erase(std::unique(points.begin(), points.end(), [](double a, double b) { return b - a < 1e-9; }),
			points.end());

		// keep the total node count in check for extreme settings, and make sure short or
		// slowly modulated stretches still get a smooth curve
		const double span = pEnd - pStart;
		const double estimate = dense ? span * detail + points.size() : points.size();
		double gridStep = 1.0 / std::max(detail, 1);
		if (dense && (amplitudeActive() || shape == Shape::Custom)) { gridStep = std::min(gridStep, span / 64.0); }
		else if (dense) { gridStep = std::min(gridStep, span / 16.0); }
		if (estimate > MaxNodes) { gridStep = std::max(gridStep, span / std::max<double>(MaxNodes - points.size(), 1.0)); }

		const auto level = [&](double p) { return low + static_cast<float>(sample(p, warp)) * (high - low); };

		float held = level(pStart + eps);
		for (std::size_t i = 0; i < points.size(); ++i)
		{
			const double p = points[i];
			const bool first = i == 0;
			const bool last = i + 1 == points.size();

			// a quantised curve holds its last level up to the point, then jumps
			const float outV = last ? level(p - eps) : level(p + eps);
			float in = first || last ? outV : level(p - eps);
			if (dense && quantised && !first && !last) { in = held; }
			out.push_back({toTick(p), in, outV});
			held = outV;

			if (last || !dense) { continue; }

			const double next = points[i + 1];
			for (double q = p + gridStep; q < next - eps; q += gridStep)
			{
				const float v = level(q);
				if (quantised)
				{
					if (v == held) { continue; }
					out.push_back({toTick(q), held, v});
					held = v;
				}
				else
				{
					out.push_back({toTick(q), v, v});
				}
			}
		}

		return out;
	}

private:
	//! Maps between time (0..1) and position in cycles, taking the frequency curve into account
	struct Warp
	{
		explicit Warp(const FunctionGenerator& g)
			: phase(g.phase)
			, cycles(g.cycles)
		{
			if (!g.frequencyActive()) { return; }

			// accumulated phase for the curve's rate: faster where the curve is high
			cumulative.assign(Resolution + 1, 0.0);
			for (int i = 1; i <= Resolution; ++i)
			{
				const double x = (i - 0.5) / Resolution;
				const double rate = std::pow(2.0, (2.0 * g.frequency.at(x) - 1.0) * g.frequencyRange);
				cumulative[i] = cumulative[i - 1] + rate;
			}
			for (auto& c : cumulative) { c /= cumulative.back(); }
		}

		//! Time (0..1) at which the position `p` (in cycles) is reached
		double timeAt(double p) const
		{
			const double f = std::clamp((p - phase) / cycles, 0.0, 1.0);
			if (cumulative.empty()) { return f; }

			const auto hi = std::lower_bound(cumulative.begin(), cumulative.end(), f);
			if (hi == cumulative.begin()) { return 0.0; }
			const auto lo = hi - 1;
			const double span = *hi - *lo;
			const double frac = span > 0.0 ? (f - *lo) / span : 0.0;
			return (static_cast<double>(lo - cumulative.begin()) + frac) / Resolution;
		}

		static constexpr int Resolution = 512;
		double phase;
		double cycles;
		std::vector<double> cumulative;
	};

	//! The finished normalised value at `p`: shape, bend, invert, amplitude envelope, quantise
	double sample(double p, const Warp& warp) const
	{
		const double t = warp.timeAt(p);
		double v = base(p, t);

		if (usesBend() && bend != 0.0) { v = std::pow(std::clamp(v, 0.0, 1.0), std::pow(4.0, bend)); }
		if (invert) { v = 1.0 - v; }

		if (amplitudeActive())
		{
			// shapes made of flat parts keep their level, so the envelope is read where each part starts
			double ampT = t;
			const double fl = std::floor(p);
			const double frac = p - fl;
			if (shape == Shape::Square) { ampT = warp.timeAt(std::max(fl + (frac < symmetry ? 0.0 : symmetry), phase)); }
			else if (shape == Shape::Noise) { ampT = warp.timeAt(std::max(fl + std::floor(frac * cells) / cells, phase)); }
			v = 0.5 + (v - 0.5) * amplitude.at(ampT);
		}

		if (steps > 1)
		{
			const double n = steps - 1;
			v = std::round(v * n) / n;
		}
		return v;
	}

	//! The plain shape (0..1) at position `p` and time `t`
	double base(double p, double t) const
	{
		const double fl = std::floor(p);
		const double frac = p - fl;
		switch (shape)
		{
			case Shape::Sine: return 0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * warpPhase(frac));
			case Shape::Triangle: return 1.0 - std::abs(2.0 * warpPhase(frac) - 1.0);
			case Shape::SawUp: return frac;
			case Shape::SawDown: return 1.0 - frac;
			case Shape::Square: return frac < symmetry ? 1.0 : 0.0;
			case Shape::Noise:
			{
				const long cell = static_cast<long>(fl) * cells + static_cast<long>(std::floor(frac * cells));
				return random(cell);
			}
			case Shape::SmoothNoise:
			{
				const double x = frac * cells;
				const long cell = static_cast<long>(fl) * cells + static_cast<long>(std::floor(x));
				double u = x - std::floor(x);
				u = u * u * (3.0 - 2.0 * u);
				return random(cell) + (random(cell + 1) - random(cell)) * u;
			}
			case Shape::Custom:
			{
				const FunctionExpression& e = compiled();
				return e.valid() ? std::clamp(0.5 + 0.5 * e.evaluate(p, t), 0.0, 1.0) : 0.5;
			}
		}
		return 0.0;
	}

	const FunctionExpression& compiled() const
	{
		if (!m_compiled || m_compiledSource != expression)
		{
			auto e = std::make_shared<FunctionExpression>();
			m_error = e->compile(expression);
			m_compiled = std::move(e);
			m_compiledSource = expression;
		}
		return *m_compiled;
	}

	//! Squeezes the first part of a cycle to `symmetry` of its length
	double warpPhase(double frac) const
	{
		const double s = std::clamp(symmetry, 0.01, 0.99);
		return frac < s ? 0.5 * frac / s : 0.5 + 0.5 * (frac - s) / (1.0 - s);
	}

	double random(long cell) const
	{
		// splitmix-style integer hash, deterministic for a given seed
		auto z = static_cast<std::uint64_t>(cell) * 0x9E3779B97F4A7C15ull + seed * 0xD1B54A32D192ED03ull;
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
		z ^= z >> 31;
		return static_cast<double>(z >> 11) / static_cast<double>(1ull << 53);
	}

	mutable std::shared_ptr<const FunctionExpression> m_compiled;
	mutable std::string m_compiledSource;
	mutable std::string m_error;
};

} // namespace lmms

#endif // LMMS_FUNCTION_GENERATOR_H
