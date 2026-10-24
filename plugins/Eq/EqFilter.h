/*
 * eqfilter.cpp - defination of EqFilterclass.
 *
 * Copyright (c) 2014 David French <dave/dot/french3/at/googlemail/dot/com>
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

#ifndef EQFILTER_H
#define EQFILTER_H

#include <array>
#include <cassert>
#include <numbers>
#include <type_traits>

#ifdef __SSE2__
#include <emmintrin.h>
#endif

#include "BasicFilters.h"
#include "SampleFrame.h"
#include "lmms_math.h"

namespace lmms
{

///
/// \brief The EqFilter class.
/// A stereo biquad with freq, res, and gain controls.
/// Processes a period at a time, with recalculation of coefficents
/// upon parameter changes. The intention is to use this as a bass class, children override
/// the calcCoefficents() function, providing the coefficents a1, a2, b0, b1, b2.
///
class EqFilter
{
public:
	EqFilter() :
		m_sampleRate(0),
		m_freq(0),
		m_res(0),
		m_gain(0),
		m_bw(0)
	{

	}




	virtual inline void setSampleRate( int sampleRate )
	{
		if( sampleRate != m_sampleRate )
		{
			m_sampleRate = sampleRate;
			calcCoefficents();
		}
	}




	virtual inline void setFrequency( float freq ){
		if ( freq != m_freq )
		{
			m_freq = freq;
			calcCoefficents();
		}
	}




	virtual inline void setQ( float res )
	{
		if ( res != m_res )
		{
			m_res = res;
			calcCoefficents();
		}
	}




	virtual inline void setGain( float gain )
	{
		if ( gain != m_gain )
		{
			m_gain = gain;
			calcCoefficents();
		}
	}



	virtual inline void setParameters( float sampleRate, float freq, float res, float gain )
	{
		bool hasChanged = ( sampleRate != m_sampleRate ||
		                    freq != m_freq ||
		                    res != m_res ||
		                    gain != m_gain );
		if ( hasChanged )
		{
			m_sampleRate = sampleRate;
			m_freq = freq;
			m_res = res;
			m_gain = gain;
			calcCoefficents();
		}
	}


	//! Most filters processChain() runs at once
	static constexpr int MaxChainLength = 16;

	//! @brief Filter a whole period through several filters in series
	//!
	//! A biquad has to wait for its previous output on every sample, so a single
	//! filter leaves the CPU mostly idle. Running every filter on each frame
	//! before moving to the next lets the work of different filters overlap.
	//!
	//! When a filter's coefficients changed since the last period, its old and new
	//! versions both run and its output crossfades between them over the period,
	//! reducing pops, clicks and dc bias offsets. The other filters are unaffected.
	//!
	//! @param count At most MaxChainLength
	static void processChain( EqFilter* const* filters, int count, SampleFrame* buf, const f_cnt_t frames )
	{
		assert( count <= MaxChainLength );

		// Left and right run in lanes 0 and 1. Lanes 2 and 3 run the new
		// coefficients of a fading filter, and otherwise repeat lanes 0 and 1,
		// so the signal between filters is always l, r, l, r.
		struct Lanes { Float4 a1, a2, b0, b1, b2, z1, z2; bool fading; };
		std::array<Lanes, MaxChainLength> s;
		bool anyFading = false;
		for( int i = 0; i < count; ++i )
		{
			const EqFilter& filter = *filters[i];
			const Section& c = filter.m_current;
			const Section& t = filter.m_fading ? filter.m_target : c;
			s[i] = { Float4( c.a1, c.a1, t.a1, t.a1 ), Float4( c.a2, c.a2, t.a2, t.a2 ),
				Float4( c.b0, c.b0, t.b0, t.b0 ), Float4( c.b1, c.b1, t.b1, t.b1 ), Float4( c.b2, c.b2, t.b2, t.b2 ),
				Float4( c.z1[0], c.z1[1], t.z1[0], t.z1[1] ), Float4( c.z2[0], c.z2[1], t.z2[0], t.z2[1] ),
				filter.m_fading };
			anyFading = anyFading || filter.m_fading;
		}

		// compiled twice, so periods without a fade don't pay for it
		const auto run = [&]( auto withFades )
		{
			const float step = frames > 1 ? 1.0f / ( frames - 1 ) : 1.0f;
			for( f_cnt_t f = 0; f < frames; ++f )
			{
				Float4 x( buf[f][0], buf[f][1], buf[f][0], buf[f][1] );
				[[maybe_unused]] Float4 weight;
				if constexpr( decltype( withFades )::value )
				{
					const float progress = frames > 1 ? f * step : 1.0f;
					weight = Float4( 1.0f - progress, 1.0f - progress, progress, progress );
				}
				for( int i = 0; i < count; ++i )
				{
					Lanes& v = s[i];
					x = biquadTick( x, v.z1, v.z2, v.a1, v.a2, v.b0, v.b1, v.b2 );
					if constexpr( decltype( withFades )::value )
					{
						if( v.fading )
						{
							// ( 1 - progress ) * old + progress * new, in both halves
							x = x * weight;
							x = x + x.swapHalves();
						}
					}
				}
				const auto out = x.toArray();
				buf[f][0] = out[0];
				buf[f][1] = out[1];
			}
		};
		if( anyFading ) { run( std::true_type{} ); }
		else { run( std::false_type{} ); }

		for( int i = 0; i < count; ++i )
		{
			EqFilter& filter = *filters[i];
			if( filter.m_fading )
			{
				filter.m_current = filter.m_target;
				filter.m_fading = false;
			}
			const int lane = s[i].fading ? 2 : 0;
			const auto z1 = s[i].z1.toArray();
			const auto z2 = s[i].z2.toArray();
			filter.m_current.z1 = { z1[lane], z1[lane + 1] };
			filter.m_current.z2 = { z2[lane], z2[lane + 1] };
			filter.m_running = true;
		}
	}


	//! @brief Mark the filter as switched off for this period
	//!
	//! Its old state is stale by the time it is switched on again, so it then
	//! starts from silence with its latest coefficients instead of fading in.
	void stop()
	{
		if( !m_running ) { return; }
		m_running = false;
		m_fading = false;
		m_current = m_target;
		m_current.z1 = { 0, 0 };
		m_current.z2 = { 0, 0 };
	}


protected:
	///
	/// \brief calcCoefficents
	///  Override this in child classes to provide the coefficents, based on
	///  Freq, Res and Gain
	virtual void calcCoefficents(){
		setCoeffs( 0, 0, 0, 0, 0 );

	}

	inline void setCoeffs( float a1, float a2, float b0, float b1, float b2 )
	{
		// the new filter picks up from the current state, then takes over during the next period
		m_target = m_current;
		m_target.a1 = a1;
		m_target.a2 = a2;
		m_target.b0 = b0;
		m_target.b1 = b1;
		m_target.b2 = b2;
		// a filter that isn't running has no old output to fade from
		if( m_running ) { m_fading = true; }
		else { m_current = m_target; }
	}






	float m_sampleRate;
	float m_freq;
	float m_res;
	float m_gain;
	float m_bw;

private:
	//! The coefficients and stereo state of a biquad in transposed direct form II
	struct Section
	{
		float a1 = 0, a2 = 0, b0 = 0, b1 = 0, b2 = 0;
		std::array<float, 2> z1 = {0, 0};
		std::array<float, 2> z2 = {0, 0};
	};

	//! Four floats processChain() works on at once
#ifdef __SSE2__
	struct Float4
	{
		__m128 v;
		Float4() = default;
		explicit Float4( __m128 v ) : v( v ) {}
		Float4( float a, float b, float c, float d ) : v( _mm_setr_ps( a, b, c, d ) ) {}
		//! Lanes 2, 3, 0, 1
		Float4 swapHalves() const { return Float4( _mm_shuffle_ps( v, v, _MM_SHUFFLE( 1, 0, 3, 2 ) ) ); }
		std::array<float, 4> toArray() const
		{
			alignas(16) std::array<float, 4> out;
			_mm_store_ps( out.data(), v );
			return out;
		}
		friend Float4 operator+( Float4 a, Float4 b ) { return Float4( _mm_add_ps( a.v, b.v ) ); }
		friend Float4 operator-( Float4 a, Float4 b ) { return Float4( _mm_sub_ps( a.v, b.v ) ); }
		friend Float4 operator*( Float4 a, Float4 b ) { return Float4( _mm_mul_ps( a.v, b.v ) ); }
	};
#else
	struct Float4
	{
		std::array<float, 4> v;
		Float4() = default;
		Float4( float a, float b, float c, float d ) : v{ a, b, c, d } {}
		//! Lanes 2, 3, 0, 1
		Float4 swapHalves() const { return Float4( v[2], v[3], v[0], v[1] ); }
		std::array<float, 4> toArray() const { return v; }
		friend Float4 operator+( Float4 a, Float4 b )
		{
			return Float4( a.v[0] + b.v[0], a.v[1] + b.v[1], a.v[2] + b.v[2], a.v[3] + b.v[3] );
		}
		friend Float4 operator-( Float4 a, Float4 b )
		{
			return Float4( a.v[0] - b.v[0], a.v[1] - b.v[1], a.v[2] - b.v[2], a.v[3] - b.v[3] );
		}
		friend Float4 operator*( Float4 a, Float4 b )
		{
			return Float4( a.v[0] * b.v[0], a.v[1] * b.v[1], a.v[2] * b.v[2], a.v[3] * b.v[3] );
		}
	};
#endif

	Section m_current;
	//! The latest coefficients, which m_current fades to during the next period
	Section m_target;
	bool m_fading = false;
	//! Whether the filter ran in the last period
	bool m_running = false;
};




///
/// \brief The EqHp12Filter class
/// A 2 pole High Pass Filter
/// Coefficent calculations from http://www.musicdsp.org/files/Audio-EQ-Cookbook.txt
class EqHp12Filter : public EqFilter
{
public :
	void calcCoefficents() override
	{

		// calc intermediate
		float w0 = 2 * std::numbers::pi_v<float> * m_freq / m_sampleRate;
		float c = std::cos(w0);
		float s = std::sin(w0);
		float alpha = s / ( 2 * m_res );

		//calc coefficents
		float b0 = (1 + c) * 0.5;
		float b1 = (-(1 + c));
		float b2 = (1 + c) * 0.5;
		float a0 = 1 + alpha;
		float a1 = (-2 * c);
		float a2 = 1 - alpha;

		//normalise
		b0 /= a0;
		b1 /= a0;
		b2 /= a0;
		a1 /= a0;
		a2 /= a0;

		a0 = 1;

		setCoeffs( a1, a2, b0, b1, b2 );


	}
};




///
/// \brief The EqLp12Filter class.
/// A 2 pole low pass filter
/// Coefficent calculations from http://www.musicdsp.org/files/Audio-EQ-Cookbook.txt
///
class EqLp12Filter : public EqFilter
{
public :
	void calcCoefficents() override
	{

		// calc intermediate
		float w0 = 2 * std::numbers::pi_v<float> * m_freq / m_sampleRate;
		float c = std::cos(w0);
		float s = std::sin(w0);
		float alpha = s / ( 2 * m_res );

		//calc coefficents
		float b0 = (1 - c) * 0.5;
		float b1 = 1 - c;
		float b2 = (1 - c) * 0.5;
		float a0 = 1 + alpha;
		float a1 = -2 * c;
		float a2 = 1 - alpha;

		//normalise
		b0 /= a0;
		b1 /= a0;
		b2 /= a0;
		a1 /= a0;
		a2 /= a0;

		a0 = 1;

		setCoeffs( a1, a2, b0, b1, b2 );
	}
};



///
/// \brief The EqPeakFilter class
/// A Peak Filter
/// Coefficent calculations from http://www.musicdsp.org/files/Audio-EQ-Cookbook.txt
///
class EqPeakFilter : public EqFilter
{
public:


	void calcCoefficents() override
	{
		using namespace std::numbers;
		// calc intermediate
		float w0 = 2 * pi_v<float> * m_freq / m_sampleRate;
		float c = std::cos(w0);
		float s = std::sin(w0);
		float A = fastPow10f(m_gain * 0.025);
		float alpha = s * std::sinh(ln2 / 2 * m_bw * w0 / std::sin(w0));

		//calc coefficents
		float b0 = 1 + alpha * A;
		float b1 = -2 * c;
		float b2 = 1 - alpha * A;
		float a0 = 1 + alpha / A;
		float a1 = -2 * c;
		float a2 = 1 - alpha / A;

		//normalise
		b0 /= a0;
		b1 /= a0;
		b2 /= a0;
		a1 /= a0;
		a2 /= a0;
		a0 = 1;

		setCoeffs( a1, a2, b0, b1, b2 );
	}

	inline void setParameters( float sampleRate, float freq, float bw, float gain ) override
	{
		bool hasChanged = false;
		if( sampleRate != m_sampleRate )
		{
			m_sampleRate = sampleRate;
			hasChanged = true;
		}
		if ( freq != m_freq )
		{
			m_freq = freq;
			hasChanged = true;
		}
		if ( bw != m_bw )
		{
			m_bw = bw;
			hasChanged = true;
		}
		if ( gain != m_gain )
		{
			m_gain = gain;
			hasChanged = true;
		}

		if ( hasChanged ) { calcCoefficents(); }
	}
};




class EqLowShelfFilter : public EqFilter
{
public :
	void calcCoefficents() override
	{

		// calc intermediate
		float w0 = 2 * std::numbers::pi_v<float> * m_freq / m_sampleRate;
		float c = std::cos(w0);
		float s = std::sin(w0);
		float A = fastPow10f(m_gain * 0.025);
		// float alpha = s / (2 * m_res);
		float beta = std::sqrt(A) / m_res;

		//calc coefficents
		float b0 = A * ((A + 1) - (A - 1) * c + beta * s);
		float b1 = 2 * A * ((A - 1) - (A + 1) * c);
		float b2 = A * ((A + 1) - (A - 1) * c - beta * s);
		float a0 = (A + 1) + (A - 1) * c + beta * s;
		float a1 = -2 * ((A - 1) + (A + 1) * c);
		float a2 = (A + 1) + (A - 1) * c - beta * s;

		//normalise
		b0 /= a0;
		b1 /= a0;
		b2 /= a0;
		a1 /= a0;
		a2 /= a0;

		a0 = 1;

		setCoeffs( a1, a2, b0, b1, b2 );


	}
};

class EqHighShelfFilter : public EqFilter
{
public :
	void calcCoefficents() override
	{

		// calc intermediate
		float w0 = 2 * std::numbers::pi_v<float> * m_freq / m_sampleRate;
		float c = std::cos(w0);
		float s = std::sin(w0);
		float A = fastPow10f(m_gain * 0.025);
		float beta = std::sqrt(A) / m_res;

		//calc coefficents
		float b0 = A * ((A + 1) + (A - 1) * c + beta * s);
		float b1 = -2 * A * ((A - 1) + (A + 1) * c);
		float b2 = A * ((A + 1) + (A - 1) * c - beta * s);
		float a0 = (A + 1) - (A - 1) * c + beta * s;
		float a1 = 2 * ((A - 1) - (A + 1) * c);
		float a2 = (A + 1) - (A - 1) * c - beta * s;

		//normalise
		b0 /= a0;
		b1 /= a0;
		b2 /= a0;
		a1 /= a0;
		a2 /= a0;
		a0 = 1;

		setCoeffs( a1, a2, b0, b1, b2 );
	}
};




class EqLinkwitzRiley : public StereoLinkwitzRiley
{
public:
	EqLinkwitzRiley() :
		StereoLinkwitzRiley( 44100),
		m_freq(0 ),
		m_sr( 1 )
	{
	}

	virtual inline void setSR( int sampleRate )
	{
		if( sampleRate != m_sr )
		{
			m_sr = sampleRate;
			setSampleRate( sampleRate );
			setLowpass(m_freq);
		}
	}




	virtual inline void setFrequency( float freq ){
		if ( freq != m_freq )
		{
			m_freq = freq;
			setLowpass(m_freq);
		}
	}




	virtual void processBuffer( SampleFrame* buf, const f_cnt_t frames )
	{
		for ( f_cnt_t f = 0 ; f < frames ; ++f)
		{
			buf[f][0] = update( buf[f][0] , 0);
			buf[f][1] = update( buf[f][1] , 1);
		}
	}
protected:

	float m_freq;
	int m_sr;


};


} // namespace lmms


#endif // EQFILTER_H
