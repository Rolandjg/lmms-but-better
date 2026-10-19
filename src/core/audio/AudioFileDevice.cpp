/*
 * AudioFileDevice.cpp - base-class for audio-device-classes which write
 *                       their output into a file
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include <QCoreApplication>

#include "AudioFileDevice.h"

namespace lmms
{

AudioFileDevice::AudioFileDevice( OutputSettings const & outputSettings,
					const ch_cnt_t _channels,
					const QString & _file,
					AudioEngine*  _audioEngine ) :
	AudioDevice( _channels, _audioEngine ),
	m_outputFile( _file ),
	m_outputSettings(outputSettings)
{
	setSampleRate( outputSettings.getSampleRate() );

	// Failures are reported by whoever started the export (see ProjectRenderer::errorString())
	if( m_outputFile.open( QFile::WriteOnly | QFile::Truncate ) == false )
	{
		setError(QCoreApplication::translate("AudioFileDevice", "Could not open %1 for writing: %2")
			.arg(_file, m_outputFile.errorString()));
	}
}




AudioFileDevice::~AudioFileDevice()
{
	m_outputFile.close();
}




int AudioFileDevice::writeData( const void* data, int len )
{
	if( m_outputFile.isOpen() )
	{
		const auto written = m_outputFile.write( (const char *) data, len );
		if (written != len)
		{
			setError(QCoreApplication::translate("AudioFileDevice", "Could not write to %1: %2")
				.arg(m_outputFile.fileName(), m_outputFile.errorString()));
		}
		return written;
	}

	return -1;
}




void AudioFileDevice::setError(const QString& error)
{
	if (m_error.isEmpty()) { m_error = error; }
}

} // namespace lmms
