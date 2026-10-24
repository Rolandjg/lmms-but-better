/* EqAnalyser.cpp - implementation of the EqAnalyser class
*
* Copyright (c) 2014-2017, David French <dave/dot/french3/at/googlemail/dot/com>
*
* This file is part of LMMS - https://lmms.io
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

#include "EqAnalyser.h"

#include <cmath>
#include <numbers>

#include "AudioEngine.h"
#include "Engine.h"

namespace lmms
{


EqAnalyser::EqAnalyser() :
	m_framesFilledUp ( 0 ),
	m_energy ( 0 ),
	m_sampleRate ( 1 ),
	m_active ( true )
{
	using namespace std::numbers;
	m_inProgress=false;
	m_specBuf = ( fftwf_complex * ) fftwf_malloc( ( FFT_BUFFER_SIZE + 1 ) * sizeof( fftwf_complex ) );
	m_fftPlan = fftwf_plan_dft_r2c_1d( FFT_BUFFER_SIZE*2, m_buffer, m_specBuf, FFTW_MEASURE );

	//initialize Blackman-Harris window, constants taken from
	//https://en.wikipedia.org/wiki/Window_function#A_list_of_window_functions
	const float a0 = 0.35875f;
	const float a1 = 0.48829f;
	const float a2 = 0.14128f;
	const float a3 = 0.01168f;

	for (auto i = std::size_t{0}; i < FFT_BUFFER_SIZE; i++)
	{
		m_fftWindow[i] = a0 - a1 * std::cos(2 * pi_v<float> * i / static_cast<float>(FFT_BUFFER_SIZE - 1.0))
			+ a2 * std::cos(4 * pi_v<float> * i / static_cast<float>(FFT_BUFFER_SIZE - 1.0))
			- a3 * std::cos(6 * pi_v<float> * i / static_cast<float>(FFT_BUFFER_SIZE - 1.0));
	}
	clear();
}




EqAnalyser::~EqAnalyser()
{
	fftwf_destroy_plan( m_fftPlan );
	fftwf_free( m_specBuf );
}




void EqAnalyser::analyze( SampleFrame* buf, const f_cnt_t frames )
{
	//only analyse if the view is visible
	if ( m_active )
	{
		m_inProgress=true;
		m_cleared = false;
		const int FFT_BUFFER_SIZE = 2048;
		f_cnt_t f = 0;
		if( frames > FFT_BUFFER_SIZE )
		{
			m_framesFilledUp = 0;
			f = frames - FFT_BUFFER_SIZE;
		}
		// meger channels, filling only the windowed half; the other half is zero padding.
		// The rest of the period is dropped, as the next window waits for the view to ask for it
		for( ; f < frames && m_framesFilledUp < FFT_BUFFER_SIZE; ++f )
		{
			m_buffer[m_framesFilledUp] =
					( buf[f][0] + buf[f][1] ) * 0.5;
			++m_framesFilledUp;
		}

		if( m_framesFilledUp < FFT_BUFFER_SIZE )
		{
			m_inProgress = false;
			return;
		}

		m_sampleRate = Engine::audioEngine()->outputSampleRate();
		const int LOWEST_FREQ = 0;
		const int HIGHEST_FREQ = m_sampleRate / 2;

		//apply FFT window
		for( int i = 0; i < FFT_BUFFER_SIZE; i++ )
		{
			m_buffer[i] = m_buffer[i] * m_fftWindow[i];
		}

		fftwf_execute( m_fftPlan );
		absspec( m_specBuf, m_absSpecBuf, FFT_BUFFER_SIZE+1 );

		compressbands( m_absSpecBuf, m_bands, FFT_BUFFER_SIZE+1,
					   MAX_BANDS,
					   ( int )( LOWEST_FREQ * ( FFT_BUFFER_SIZE + 1 ) / ( float )( m_sampleRate / 2 ) ),
					   ( int )( HIGHEST_FREQ * ( FFT_BUFFER_SIZE +  1) / ( float )( m_sampleRate / 2 ) ) );
		m_energy = maximum( m_bands, MAX_BANDS ) / maximum( m_buffer, FFT_BUFFER_SIZE );

		m_framesFilledUp = 0;
		m_inProgress = false;
		m_active = false;
	}
}




float EqAnalyser::getEnergy() const
{
	return m_energy;
}




int EqAnalyser::getSampleRate() const
{
	return m_sampleRate;
}




bool EqAnalyser::getActive() const
{
	return m_active;
}




void EqAnalyser::setActive(bool active)
{
	m_active = active;
}




bool EqAnalyser::getInProgress()
{
	return m_inProgress;
}




void EqAnalyser::clear()
{
	// called every period while the analysis is off, so only wipe the buffers once
	if (m_cleared) { return; }
	m_cleared = true;
	m_framesFilledUp = 0;
	m_energy = 0;
	memset( m_buffer, 0, sizeof( m_buffer ) );
	memset( m_bands, 0, sizeof( m_bands ) );
}



} // namespace lmms
