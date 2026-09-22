/*
 * TripleOscillator.cpp - powerful instrument with three oscillators
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


#include <QDomElement>
#include <QFileInfo>
#include <QPainter>
#include <cmath>
#include <numbers>
#include <vector>

#include "TripleOscillator.h"
#include "AudioEngine.h"
#include "AutomatableButton.h"
#include "lmms_math.h"
#include "ModernDsp.h"
#include "Engine.h"
#include "FileDialog.h"
#include "InstrumentTrack.h"
#include "Knob.h"
#include "LcdSpinBox.h"
#include "NotePlayHandle.h"
#include "Oscillator.h"
#include "PathUtil.h"
#include "PixmapButton.h"
#include "FontHelper.h"
#include "SampleBuffer.h"
#include "Song.h"
#include "embed.h"
#include "plugin_export.h"

namespace lmms
{


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT tripleoscillator_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"TripleOscillator",
	QT_TRANSLATE_NOOP( "PluginBrowser",
				"Three powerful oscillators you can modulate "
				"in several ways" ),
	"Tobias Doerffel <tobydox/at/users.sf.net>",
	0x0110,
	Plugin::Type::Instrument,
	new PluginPixmapLoader( "logo" ),
	nullptr,
	nullptr,
} ;

}



OscillatorObject::OscillatorObject( Model * _parent, int _idx ) :
	Model( _parent ),
	m_volumeModel( DefaultVolume / NUM_OF_OSCILLATORS, MinVolume,
			MaxVolume, 1.0f, this, tr( "Osc %1 volume" ).arg( _idx+1 ) ),
	m_panModel( DefaultPanning, PanningLeft, PanningRight, 1.0f, this,
			tr( "Osc %1 panning" ).arg( _idx+1 ) ),
	m_coarseModel( -_idx*KeysPerOctave,
			-2 * KeysPerOctave, 2 * KeysPerOctave, 1.0f, this,
			tr( "Osc %1 coarse detuning" ).arg( _idx+1 ) ),
	m_fineLeftModel( 0.0f, -100.0f, 100.0f, 1.0f, this,
			tr( "Osc %1 fine detuning left" ).arg( _idx+1 ) ),
	m_fineRightModel( 0.0f, -100.0f, 100.0f, 1.0f, this,
			tr( "Osc %1 fine detuning right" ).arg( _idx + 1 ) ),
	m_phaseOffsetModel( 0.0f, 0.0f, 360.0f, 1.0f, this,
			tr( "Osc %1 phase-offset" ).arg( _idx+1 ) ),
	m_stereoPhaseDetuningModel( 0.0f, 0.0f, 360.0f, 1.0f, this,
			tr( "Osc %1 stereo phase-detuning" ).arg( _idx+1 ) ),
	m_waveShapeModel( static_cast<int>(Oscillator::WaveShape::Sine), 0,
			Oscillator::NumWaveShapes-1, this,
			tr( "Osc %1 wave shape" ).arg( _idx+1 ) ),
	m_modulationAlgoModel( static_cast<int>(Oscillator::ModulationAlgo::SignalMix), 0,
				Oscillator::NumModulationAlgos-1, this,
				tr( "Modulation type %1" ).arg( _idx+1 ) ),
	m_useWaveTableModel(true),

	m_sampleBuffer( new SampleBuffer ),
	m_volumeLeft( 0.0f ),
	m_volumeRight( 0.0f ),
	m_detuningLeft( 0.0f ),
	m_detuningRight( 0.0f ),
	m_phaseOffsetLeft( 0.0f ),
	m_phaseOffsetRight( 0.0f ),
	m_useWaveTable( true )
{
	// Connect knobs with Oscillators' inputs
	connect( &m_volumeModel, SIGNAL( dataChanged() ),
					this, SLOT( updateVolume() ), Qt::DirectConnection );
	connect( &m_panModel, SIGNAL( dataChanged() ),
					this, SLOT( updateVolume() ), Qt::DirectConnection );
	updateVolume();

	connect( &m_coarseModel, SIGNAL( dataChanged() ),
				this, SLOT( updateDetuningLeft() ), Qt::DirectConnection );
	connect( &m_coarseModel, SIGNAL( dataChanged() ),
				this, SLOT( updateDetuningRight() ), Qt::DirectConnection );
	connect( &m_fineLeftModel, SIGNAL( dataChanged() ),
				this, SLOT( updateDetuningLeft() ), Qt::DirectConnection );
	connect( &m_fineRightModel, SIGNAL( dataChanged() ),
				this, SLOT( updateDetuningRight() ), Qt::DirectConnection );
	updateDetuningLeft();
	updateDetuningRight();

	connect( &m_phaseOffsetModel, SIGNAL( dataChanged() ),
			this, SLOT( updatePhaseOffsetLeft() ), Qt::DirectConnection );
	connect( &m_phaseOffsetModel, SIGNAL( dataChanged() ),
			this, SLOT( updatePhaseOffsetRight() ), Qt::DirectConnection );
	connect( &m_stereoPhaseDetuningModel, SIGNAL( dataChanged() ),
			this, SLOT( updatePhaseOffsetLeft() ), Qt::DirectConnection );
	connect ( &m_useWaveTableModel, SIGNAL(dataChanged()),
			this, SLOT( updateUseWaveTable()));

	updatePhaseOffsetLeft();
	updatePhaseOffsetRight();

}

void OscillatorObject::oscUserDefWaveDblClick()
{
	auto af = gui::FileDialog::openWaveformFile();
	if( af != "" )
	{
		m_sampleBuffer = SampleBuffer::fromFile(af);
		m_userAntiAliasWaveTable = Oscillator::generateAntiAliasUserWaveTable(m_sampleBuffer.get());
		// TODO:
		//m_usrWaveBtn->setToolTip(m_sampleBuffer->audioFile());
	}
}




void OscillatorObject::updateVolume()
{
	if( m_panModel.value() >= 0.0f )
	{
		const float panningFactorLeft = 1.0f - m_panModel.value()
							/ (float)PanningRight;
		m_volumeLeft = panningFactorLeft * m_volumeModel.value() /
									100.0f;
		m_volumeRight = m_volumeModel.value() / 100.0f;
	}
	else
	{
		m_volumeLeft = m_volumeModel.value() / 100.0f;
		const float panningFactorRight = 1.0f + m_panModel.value()
							/ (float)PanningRight;
		m_volumeRight = panningFactorRight * m_volumeModel.value() /
									100.0f;
	}
}




void OscillatorObject::updateDetuningLeft()
{
	m_detuningLeft = std::exp2((m_coarseModel.value() * 100.0f + m_fineLeftModel.value()) / 1200.0f)
		/ Engine::audioEngine()->outputSampleRate();
}




void OscillatorObject::updateDetuningRight()
{
	m_detuningRight = std::exp2((m_coarseModel.value() * 100.0f + m_fineRightModel.value()) / 1200.0f)
		/ Engine::audioEngine()->outputSampleRate();
}




void OscillatorObject::updatePhaseOffsetLeft()
{
	m_phaseOffsetLeft = ( m_phaseOffsetModel.value() +
				m_stereoPhaseDetuningModel.value() ) / 360.0f;
}




void OscillatorObject::updatePhaseOffsetRight()
{
	m_phaseOffsetRight = m_phaseOffsetModel.value() / 360.0f;
}

void OscillatorObject::updateUseWaveTable()
{
	m_useWaveTable = m_useWaveTableModel.value();
}




TripleOscillator::TripleOscillator( InstrumentTrack * _instrument_track ) :
	Instrument( _instrument_track, &tripleoscillator_plugin_descriptor ),
	m_unisonVoicesModel(this, tr("Unison voices")),
	m_unisonDetuneModel(15.f, 0.f, 100.f, 0.1f, this, tr("Unison detune")),
	m_unisonSpreadModel(60.f, 0.f, 100.f, 0.1f, this, tr("Unison stereo spread"))
{
	for (int v = 1; v <= MaxUnisonVoices; ++v)
	{
		m_unisonVoicesModel.addItem(QString::number(v));
	}

	for( int i = 0; i < NUM_OF_OSCILLATORS; ++i )
	{
		m_osc[i] = new OscillatorObject( this, i );

	}

	connect( Engine::audioEngine(), SIGNAL( sampleRateChanged() ),
			this, SLOT( updateAllDetuning() ) );
}




void TripleOscillator::saveSettings( QDomDocument & _doc, QDomElement & _this )
{
	for( int i = 0; i < NUM_OF_OSCILLATORS; ++i )
	{
		QString is = QString::number( i );
		m_osc[i]->m_volumeModel.saveSettings( _doc, _this, "vol" + is );
		m_osc[i]->m_panModel.saveSettings( _doc, _this, "pan" + is );
		m_osc[i]->m_coarseModel.saveSettings( _doc, _this, "coarse"
									+ is );
		m_osc[i]->m_fineLeftModel.saveSettings( _doc, _this, "finel" +
									is );
		m_osc[i]->m_fineRightModel.saveSettings( _doc, _this, "finer" +
									is );
		m_osc[i]->m_phaseOffsetModel.saveSettings( _doc, _this,
							"phoffset" + is );
		m_osc[i]->m_stereoPhaseDetuningModel.saveSettings( _doc, _this,
							"stphdetun" + is );
		m_osc[i]->m_waveShapeModel.saveSettings( _doc, _this,
							"wavetype" + is );
		m_osc[i]->m_modulationAlgoModel.saveSettings( _doc, _this,
					"modalgo" + QString::number( i+1 ) );
		m_osc[i]->m_useWaveTableModel.saveSettings( _doc, _this,
					"useWaveTable" + QString::number (i+1 ) );
		_this.setAttribute( "userwavefile" + is,
					m_osc[i]->m_sampleBuffer->audioFile() );
	}
	m_unisonVoicesModel.saveSettings(_doc, _this, "unisonVoices");
	m_unisonDetuneModel.saveSettings(_doc, _this, "unisonDetune");
	m_unisonSpreadModel.saveSettings(_doc, _this, "unisonSpread");
}




void TripleOscillator::loadSettings( const QDomElement & _this )
{
	for( int i = 0; i < NUM_OF_OSCILLATORS; ++i )
	{
		const QString is = QString::number( i );
		m_osc[i]->m_volumeModel.loadSettings( _this, "vol" + is );
		m_osc[i]->m_panModel.loadSettings( _this, "pan" + is );
		m_osc[i]->m_coarseModel.loadSettings( _this, "coarse" + is );
		m_osc[i]->m_fineLeftModel.loadSettings( _this, "finel" + is );
		m_osc[i]->m_fineRightModel.loadSettings( _this, "finer" + is );
		m_osc[i]->m_phaseOffsetModel.loadSettings( _this,
							"phoffset" + is );
		m_osc[i]->m_stereoPhaseDetuningModel.loadSettings( _this,
							"stphdetun" + is );
		m_osc[i]->m_waveShapeModel.loadSettings( _this, "wavetype" +
									is );
		m_osc[i]->m_modulationAlgoModel.loadSettings( _this,
					"modalgo" + QString::number( i+1 ) );
		m_osc[i]->m_useWaveTableModel.loadSettings( _this,
							"useWaveTable" + QString::number (i+1 ) );

		if (auto userWaveFile = _this.attribute("userwavefile" + is); !userWaveFile.isEmpty())
		{
			if (QFileInfo(PathUtil::toAbsolute(userWaveFile)).exists())
			{
				m_osc[i]->m_sampleBuffer = SampleBuffer::fromFile(userWaveFile);
				m_osc[i]->m_userAntiAliasWaveTable = Oscillator::generateAntiAliasUserWaveTable(m_osc[i]->m_sampleBuffer.get());
			}
			else { Engine::getSong()->collectError(QString("%1: %2").arg(tr("Sample not found"), userWaveFile)); }
		}
	}
	// Unison was added later; older projects load with a single voice
	m_unisonVoicesModel.loadSettings(_this, "unisonVoices");
	m_unisonDetuneModel.loadSettings(_this, "unisonDetune");
	m_unisonSpreadModel.loadSettings(_this, "unisonSpread");
}




QString TripleOscillator::nodeName() const
{
	return( tripleoscillator_plugin_descriptor.name );
}




//! Per-voice copies of the oscillator parameters. The Oscillator class keeps references
//! to these, so they must stay at a stable address for the lifetime of the note.
struct TripleOscillator::UnisonVoice
{
	std::array<float, NUM_OF_OSCILLATORS> detuneLeft{}, detuneRight{};
	std::array<float, NUM_OF_OSCILLATORS> phaseLeft{}, phaseRight{};
	std::array<float, NUM_OF_OSCILLATORS> volumeLeft{}, volumeRight{};
	std::array<float, NUM_OF_OSCILLATORS> randomPhase{};
	float detuneRatio = 1.f;
	float gainLeft = 1.f;
	float gainRight = 1.f;
	Oscillator* left = nullptr;
	Oscillator* right = nullptr;
};

struct TripleOscillator::NoteData
{
	std::vector<UnisonVoice> voices;
	std::vector<SampleFrame> scratch;

	~NoteData()
	{
		for (auto& voice : voices)
		{
			delete voice.left;
			delete voice.right;
		}
	}
};




void TripleOscillator::playNote( NotePlayHandle * _n,
						SampleFrame* _working_buffer )
{
	if (!_n->m_pluginData)
	{
		auto data = new NoteData;
		const int voiceCount = std::clamp(m_unisonVoicesModel.value() + 1, 1, MaxUnisonVoices);
		// Sized once: oscillators hold references into these elements
		data->voices.resize(voiceCount);
		data->scratch.resize(Engine::audioEngine()->framesPerPeriod());

		const float detune = m_unisonDetuneModel.value();
		const float spread = m_unisonSpreadModel.value() * 0.01f;
		for (int v = 0; v < voiceCount; ++v)
		{
			auto& voice = data->voices[v];
			const auto layout = dsp::unisonVoice(v, voiceCount, detune, spread);
			voice.detuneRatio = layout.detuneRatio;
			voice.gainLeft = layout.gainLeft;
			voice.gainRight = layout.gainRight;
			// Each voice starts at a random point in time so the stack doesn't sum into one loud
			// click. Offsetting every oscillator by the same time (not the same phase) keeps the
			// phase relationships between the oscillators, which MIX, AM, PM and sync rely on.
			const float cycles = voiceCount > 1 ? fastRand(1.f) : 0.f;
			const float sampleRate = Engine::audioEngine()->outputSampleRate();
			for (int i = 0; i < NUM_OF_OSCILLATORS; ++i)
			{
				const float multiplier = m_osc[i]->m_detuningLeft * sampleRate;
				voice.randomPhase[i] = cycles * multiplier - std::floor(cycles * multiplier);
			}
		}

		for (auto& voice : data->voices)
		{
			for (int i = 0; i < NUM_OF_OSCILLATORS; ++i)
			{
				voice.detuneLeft[i] = m_osc[i]->m_detuningLeft * voice.detuneRatio;
				voice.detuneRight[i] = m_osc[i]->m_detuningRight * voice.detuneRatio;
				voice.phaseLeft[i] = m_osc[i]->m_phaseOffsetLeft + voice.randomPhase[i];
				voice.phaseRight[i] = m_osc[i]->m_phaseOffsetRight + voice.randomPhase[i];
				voice.volumeLeft[i] = m_osc[i]->m_volumeLeft;
				voice.volumeRight[i] = m_osc[i]->m_volumeRight;
			}

			Oscillator* subLeft = nullptr;
			Oscillator* subRight = nullptr;
			// Build the modulation chain from the last oscillator up to the first
			for (int i = NUM_OF_OSCILLATORS - 1; i >= 0; --i)
			{
				auto left = new Oscillator(&m_osc[i]->m_waveShapeModel, &m_osc[i]->m_modulationAlgoModel,
					_n->frequency(), voice.detuneLeft[i], voice.phaseLeft[i], voice.volumeLeft[i], subLeft);
				auto right = new Oscillator(&m_osc[i]->m_waveShapeModel, &m_osc[i]->m_modulationAlgoModel,
					_n->frequency(), voice.detuneRight[i], voice.phaseRight[i], voice.volumeRight[i], subRight);
				for (auto osc : {left, right})
				{
					osc->setUseWaveTable(m_osc[i]->m_useWaveTable);
					osc->setUserWave(m_osc[i]->m_sampleBuffer);
					osc->setUserAntiAliasWaveTable(m_osc[i]->m_userAntiAliasWaveTable);
				}
				subLeft = left;
				subRight = right;
			}
			voice.left = subLeft;
			voice.right = subRight;
		}

		_n->m_pluginData = data;
	}

	auto data = static_cast<NoteData*>(_n->m_pluginData);

	const f_cnt_t frames = _n->framesLeftForCurrentPeriod();
	const f_cnt_t offset = _n->noteOffset();
	const bool unison = data->voices.size() > 1;

	for (auto& voice : data->voices)
	{
		// Track live knob changes (the oscillators read these by reference)
		for (int i = 0; i < NUM_OF_OSCILLATORS; ++i)
		{
			voice.detuneLeft[i] = m_osc[i]->m_detuningLeft * voice.detuneRatio;
			voice.detuneRight[i] = m_osc[i]->m_detuningRight * voice.detuneRatio;
			voice.volumeLeft[i] = m_osc[i]->m_volumeLeft;
			voice.volumeRight[i] = m_osc[i]->m_volumeRight;
		}
	}

	if (!unison)
	{
		auto& voice = data->voices.front();
		voice.left->update(_working_buffer + offset, frames, 0);
		voice.right->update(_working_buffer + offset, frames, 1);
	}
	else
	{
		// Render each voice separately and sum with its stereo position and level
		zeroSampleFrames(_working_buffer + offset, frames);
		auto scratch = data->scratch.data();
		for (auto& voice : data->voices)
		{
			voice.left->update(scratch, frames, 0);
			voice.right->update(scratch, frames, 1);
			for (f_cnt_t f = 0; f < frames; ++f)
			{
				_working_buffer[offset + f][0] += scratch[f][0] * voice.gainLeft;
				_working_buffer[offset + f][1] += scratch[f][1] * voice.gainRight;
			}
		}
	}

	applyFadeIn(_working_buffer, _n);
	applyRelease( _working_buffer, _n );
}




void TripleOscillator::deleteNotePluginData( NotePlayHandle * _n )
{
	delete static_cast<NoteData*>(_n->m_pluginData);
}




gui::PluginView* TripleOscillator::instantiateView( QWidget * _parent )
{
	return new gui::TripleOscillatorView( this, _parent );
}




void TripleOscillator::updateAllDetuning()
{
	for (auto& osc : m_osc)
	{
		osc->updateDetuningLeft();
		osc->updateDetuningRight();
	}
}



namespace gui
{


class TripleOscKnob : public Knob
{
public:
	TripleOscKnob( QWidget * _parent ) :
			Knob( KnobType::Styled, _parent )
	{
		setFixedSize( 28, 35 );
	}
};

// 82, 109


TripleOscillatorView::TripleOscillatorView( Instrument * _instrument,
							QWidget * _parent ) :
	InstrumentView( _instrument, _parent )
{
	const int mod_x = 66;
	const int mod1_y = 58;
	const int mod2_y = 75;
	const int osc_y = 109;
	const int osc_h = 52;

	// TODO: clean rewrite using layouts and all that...
	auto pm_osc1_btn = new PixmapButton(this, nullptr);
	pm_osc1_btn->move( mod_x, mod1_y );
	pm_osc1_btn->setActiveGraphic( PLUGIN_NAME::getIconPixmap(
								"pm_active" ) );
	pm_osc1_btn->setInactiveGraphic( PLUGIN_NAME::getIconPixmap(
							"pm_inactive" ) );
	pm_osc1_btn->setToolTip(tr("Modulate phase of oscillator 1 by oscillator 2"));

	auto am_osc1_btn = new PixmapButton(this, nullptr);
	am_osc1_btn->move( mod_x + 35, mod1_y );
	am_osc1_btn->setActiveGraphic( PLUGIN_NAME::getIconPixmap(
								"am_active" ) );
	am_osc1_btn->setInactiveGraphic( PLUGIN_NAME::getIconPixmap(
							"am_inactive" ) );
	am_osc1_btn->setToolTip(tr("Modulate amplitude of oscillator 1 by oscillator 2"));

	auto mix_osc1_btn = new PixmapButton(this, nullptr);
	mix_osc1_btn->move( mod_x + 70, mod1_y );
	mix_osc1_btn->setActiveGraphic( PLUGIN_NAME::getIconPixmap(
							"mix_active" ) );
	mix_osc1_btn->setInactiveGraphic( PLUGIN_NAME::getIconPixmap(
							"mix_inactive" ) );
	mix_osc1_btn->setToolTip(tr("Mix output of oscillators 1 & 2"));

	auto sync_osc1_btn = new PixmapButton(this, nullptr);
	sync_osc1_btn->move( mod_x + 105, mod1_y );
	sync_osc1_btn->setActiveGraphic( PLUGIN_NAME::getIconPixmap(
							"sync_active" ) );
	sync_osc1_btn->setInactiveGraphic( PLUGIN_NAME::getIconPixmap(
							"sync_inactive" ) );
	sync_osc1_btn->setToolTip(tr("Synchronize oscillator 1 with "
							"oscillator 2" ) );

	auto fm_osc1_btn = new PixmapButton(this, nullptr);
	fm_osc1_btn->move( mod_x + 140, mod1_y );
	fm_osc1_btn->setActiveGraphic( PLUGIN_NAME::getIconPixmap(
								"fm_active" ) );
	fm_osc1_btn->setInactiveGraphic( PLUGIN_NAME::getIconPixmap(
							"fm_inactive" ) );
	fm_osc1_btn->setToolTip(tr("Modulate frequency of oscillator 1 by oscillator 2"));

	m_mod1BtnGrp = new AutomatableButtonGroup( this );
	m_mod1BtnGrp->addButton( pm_osc1_btn );
	m_mod1BtnGrp->addButton( am_osc1_btn );
	m_mod1BtnGrp->addButton( mix_osc1_btn );
	m_mod1BtnGrp->addButton( sync_osc1_btn );
	m_mod1BtnGrp->addButton( fm_osc1_btn );

	auto pm_osc2_btn = new PixmapButton(this, nullptr);
	pm_osc2_btn->move( mod_x, mod2_y );
	pm_osc2_btn->setActiveGraphic( PLUGIN_NAME::getIconPixmap(
								"pm_active" ) );
	pm_osc2_btn->setInactiveGraphic( PLUGIN_NAME::getIconPixmap(
							"pm_inactive" ) );
	pm_osc2_btn->setToolTip(tr("Modulate phase of oscillator 2 by oscillator 3"));

	auto am_osc2_btn = new PixmapButton(this, nullptr);
	am_osc2_btn->move( mod_x + 35, mod2_y );
	am_osc2_btn->setActiveGraphic( PLUGIN_NAME::getIconPixmap(
								"am_active" ) );
	am_osc2_btn->setInactiveGraphic( PLUGIN_NAME::getIconPixmap(
							"am_inactive" ) );
	am_osc2_btn->setToolTip(tr("Modulate amplitude of oscillator 2 by oscillator 3"));

	auto mix_osc2_btn = new PixmapButton(this, nullptr);
	mix_osc2_btn->move( mod_x + 70, mod2_y );
	mix_osc2_btn->setActiveGraphic( PLUGIN_NAME::getIconPixmap(
							"mix_active" ) );
	mix_osc2_btn->setInactiveGraphic( PLUGIN_NAME::getIconPixmap(
							"mix_inactive" ) );
	mix_osc2_btn->setToolTip(tr("Mix output of oscillators 2 & 3"));

	auto sync_osc2_btn = new PixmapButton(this, nullptr);
	sync_osc2_btn->move( mod_x + 105, mod2_y );
	sync_osc2_btn->setActiveGraphic( PLUGIN_NAME::getIconPixmap(
							"sync_active" ) );
	sync_osc2_btn->setInactiveGraphic( PLUGIN_NAME::getIconPixmap(
							"sync_inactive" ) );
	sync_osc2_btn->setToolTip(tr("Synchronize oscillator 2 with oscillator 3"));

	auto fm_osc2_btn = new PixmapButton(this, nullptr);
	fm_osc2_btn->move( mod_x + 140, mod2_y );
	fm_osc2_btn->setActiveGraphic( PLUGIN_NAME::getIconPixmap(
								"fm_active" ) );
	fm_osc2_btn->setInactiveGraphic( PLUGIN_NAME::getIconPixmap(
							"fm_inactive" ) );
	fm_osc2_btn->setToolTip(tr("Modulate frequency of oscillator 2 by oscillator 3"));

	m_mod2BtnGrp = new AutomatableButtonGroup( this );

	m_mod2BtnGrp->addButton( pm_osc2_btn );
	m_mod2BtnGrp->addButton( am_osc2_btn );
	m_mod2BtnGrp->addButton( mix_osc2_btn );
	m_mod2BtnGrp->addButton( sync_osc2_btn );
	m_mod2BtnGrp->addButton( fm_osc2_btn );


	for( int i = 0; i < NUM_OF_OSCILLATORS; ++i )
	{
		int knob_y = osc_y + i * osc_h;

		// setup volume-knob
		auto vk = new VolumeKnob(KnobType::Styled, this);
		vk->setFixedSize( 28, 35 );
		vk->move( 6, knob_y );
		vk->setHintText( tr( "Osc %1 volume:" ).arg(
							 i+1 ), "%" );

		// setup panning-knob
		Knob * pk = new TripleOscKnob( this );
		pk->move( 35, knob_y );
		pk->setHintText( tr("Osc %1 panning:").arg( i + 1 ), "" );

		// setup coarse-knob
		Knob * ck = new TripleOscKnob( this );
		ck->move( 82, knob_y );
		ck->setHintText( tr( "Osc %1 coarse detuning:" ).arg( i + 1 )
						 , " " + tr( "semitones" ) );

		// setup knob for left fine-detuning
		Knob * flk = new TripleOscKnob( this );
		flk->move( 111, knob_y );
		flk->setHintText( tr( "Osc %1 fine detuning left:" ).
						  arg( i + 1 ),
							" " + tr( "cents" ) );

		// setup knob for right fine-detuning
		Knob * frk = new TripleOscKnob( this );
		frk->move( 140, knob_y );
		frk->setHintText( tr( "Osc %1 fine detuning right:" ).
						  arg( i + 1 ),
							" " + tr( "cents" ) );

		// setup phase-offset-knob
		Knob * pok = new TripleOscKnob( this );
		pok->move( 188, knob_y );
		pok->setHintText( tr( "Osc %1 phase-offset:" ).
						  arg( i + 1 ),
							" " + tr( "degrees" ) );

		// setup stereo-phase-detuning-knob
		Knob * spdk = new TripleOscKnob( this );
		spdk->move( 217, knob_y );
		spdk->setHintText( tr("Osc %1 stereo phase-detuning:" ).
						arg( i + 1 ),
							" " + tr( "degrees" ) );

		int btn_y = 96 + i * osc_h;

		auto sin_wave_btn = new PixmapButton(this, nullptr);
		sin_wave_btn->move( 128, btn_y );
		sin_wave_btn->setActiveGraphic( PLUGIN_NAME::getIconPixmap(
							"sin_shape_active" ) );
		sin_wave_btn->setInactiveGraphic( PLUGIN_NAME::getIconPixmap(
							"sin_shape_inactive" ) );
		sin_wave_btn->setToolTip(
				tr( "Sine wave" ) );

		auto triangle_wave_btn = new PixmapButton(this, nullptr);
		triangle_wave_btn->move( 143, btn_y );
		triangle_wave_btn->setActiveGraphic(
			PLUGIN_NAME::getIconPixmap( "triangle_shape_active" ) );
		triangle_wave_btn->setInactiveGraphic(
			PLUGIN_NAME::getIconPixmap( "triangle_shape_inactive" ) );
		triangle_wave_btn->setToolTip(
				tr( "Triangle wave") );

		auto saw_wave_btn = new PixmapButton(this, nullptr);
		saw_wave_btn->move( 158, btn_y );
		saw_wave_btn->setActiveGraphic( PLUGIN_NAME::getIconPixmap(
							"saw_shape_active" ) );
		saw_wave_btn->setInactiveGraphic( PLUGIN_NAME::getIconPixmap(
							"saw_shape_inactive" ) );
		saw_wave_btn->setToolTip(
				tr( "Saw wave" ) );

		auto sqr_wave_btn = new PixmapButton(this, nullptr);
		sqr_wave_btn->move( 173, btn_y );
		sqr_wave_btn->setActiveGraphic( PLUGIN_NAME::getIconPixmap(
						"square_shape_active" ) );
		sqr_wave_btn->setInactiveGraphic( PLUGIN_NAME::getIconPixmap(
						"square_shape_inactive" ) );
		sqr_wave_btn->setToolTip(
				tr( "Square wave" ) );

		auto moog_saw_wave_btn = new PixmapButton(this, nullptr);
		moog_saw_wave_btn->move( 188, btn_y );
		moog_saw_wave_btn->setActiveGraphic(
			PLUGIN_NAME::getIconPixmap( "moog_saw_shape_active" ) );
		moog_saw_wave_btn->setInactiveGraphic(
			PLUGIN_NAME::getIconPixmap( "moog_saw_shape_inactive" ) );
		moog_saw_wave_btn->setToolTip(
				tr( "Moog-like saw wave" ) );

		auto exp_wave_btn = new PixmapButton(this, nullptr);
		exp_wave_btn->move( 203, btn_y );
		exp_wave_btn->setActiveGraphic( PLUGIN_NAME::getIconPixmap(
							"exp_shape_active" ) );
		exp_wave_btn->setInactiveGraphic( PLUGIN_NAME::getIconPixmap(
							"exp_shape_inactive" ) );
		exp_wave_btn->setToolTip(
				tr( "Exponential wave" ) );

		auto white_noise_btn = new PixmapButton(this, nullptr);
		white_noise_btn->move( 218, btn_y );
		white_noise_btn->setActiveGraphic(
			PLUGIN_NAME::getIconPixmap( "white_noise_shape_active" ) );
		white_noise_btn->setInactiveGraphic(
			PLUGIN_NAME::getIconPixmap( "white_noise_shape_inactive" ) );
		white_noise_btn->setToolTip(
				tr( "White noise" ) );

		auto uwb = new PixmapButton(this, nullptr);
		uwb->move( 233, btn_y );
		uwb->setActiveGraphic( PLUGIN_NAME::getIconPixmap(
							"usr_shape_active" ) );
		uwb->setInactiveGraphic( PLUGIN_NAME::getIconPixmap(
							"usr_shape_inactive" ) );
		uwb->setToolTip(tr("User-defined wave"));

		auto uwt = new PixmapButton(this, nullptr);
		uwt->move( 110, btn_y );
		uwt->setActiveGraphic( PLUGIN_NAME::getIconPixmap(
							"wavetable_active" ) );
		uwt->setInactiveGraphic( PLUGIN_NAME::getIconPixmap(
							"wavetable_inactive" ) );
		uwt->setCheckable(true);
		uwt->setToolTip(tr("Use alias-free wavetable oscillators."));

		auto wsbg = new AutomatableButtonGroup(this);

		wsbg->addButton( sin_wave_btn );
		wsbg->addButton( triangle_wave_btn );
		wsbg->addButton( saw_wave_btn );
		wsbg->addButton( sqr_wave_btn );
		wsbg->addButton( moog_saw_wave_btn );
		wsbg->addButton( exp_wave_btn );
		wsbg->addButton( white_noise_btn );
		wsbg->addButton( uwb );


		m_oscKnobs[i] = OscillatorKnobs( vk, pk, ck, flk, frk, pok,
							spdk, uwb, wsbg, uwt );
	}

	// Unison was added later: its controls sit on a strip below the original artwork
	m_unisonVoices = new LcdSpinBox(1, this, tr("Unison voices"));
	m_unisonVoices->setDisplayOffset(1);
	m_unisonVoices->setToolTip(tr("Number of stacked voices per note"));
	m_unisonVoices->move(96 - m_unisonVoices->sizeHint().width() / 2, UnisonStripY + 6);

	// Same columns as the phase knobs above
	m_unisonDetune = new TripleOscKnob(this);
	m_unisonDetune->move(188, UnisonStripY + 3);
	m_unisonDetune->setHintText(tr("Unison detune:"), " " + tr("cents"));

	m_unisonSpread = new TripleOscKnob(this);
	m_unisonSpread->move(217, UnisonStripY + 3);
	m_unisonSpread->setHintText(tr("Unison stereo spread:"), "%");

	setFixedSize(sizeHint());
}




void TripleOscillatorView::paintEvent(QPaintEvent*)
{
	static const auto artwork = PLUGIN_NAME::getIconPixmap("artwork");
	QPainter p(this);
	p.drawPixmap(0, 0, artwork);

	// Continue the artwork's plain grey below it, with a separator like the ones between the oscillators
	static const QColor grey = artwork.toImage().pixelColor(artwork.width() - 2, artwork.height() - 2);
	p.fillRect(QRect(0, UnisonStripY, width(), height() - UnisonStripY), grey);
	p.setPen(grey.darker(125));
	p.drawLine(4, UnisonStripY, width() - 5, UnisonStripY);

	// The knob bodies are printed in the artwork, so reuse the printed one under the phase-offset knob
	const QRect printedKnob(188, 109, 28, 26);
	for (const Knob* knob : {m_unisonDetune, m_unisonSpread})
	{
		p.drawPixmap(QRect(knob->pos(), printedKnob.size()), artwork, printedKnob);
	}

	auto f = adjustedToPixelSize(font(), SMALL_FONT_SIZE);
	f.setBold(true);
	p.setFont(f);
	p.setPen(QColor(90, 90, 90));
	const int labelY = UnisonStripY + 30;
	p.drawText(QRect(66, labelY, 60, 12), Qt::AlignHCenter | Qt::AlignTop, tr("VOICES"));
	p.drawText(QRect(180, labelY, 40, 12), Qt::AlignHCenter | Qt::AlignTop, tr("DET"));
	p.drawText(QRect(208, labelY, 42, 12), Qt::AlignHCenter | Qt::AlignTop, tr("SPRD"));

	f.setPixelSize(13);
	p.setFont(f);
	p.drawText(QRect(6, UnisonStripY + 2, 58, 28), Qt::AlignLeft | Qt::AlignVCenter, tr("UNISON"));
}




void TripleOscillatorView::modelChanged()
{
	auto t = castModel<TripleOscillator>();
	m_mod1BtnGrp->setModel( &t->m_osc[0]->m_modulationAlgoModel );
	m_mod2BtnGrp->setModel( &t->m_osc[1]->m_modulationAlgoModel );

	for( int i = 0; i < NUM_OF_OSCILLATORS; ++i )
	{
		m_oscKnobs[i].m_volKnob->setModel(
					&t->m_osc[i]->m_volumeModel );
		m_oscKnobs[i].m_panKnob->setModel(
					&t->m_osc[i]->m_panModel );
		m_oscKnobs[i].m_coarseKnob->setModel(
					&t->m_osc[i]->m_coarseModel );
		m_oscKnobs[i].m_fineLeftKnob->setModel(
					&t->m_osc[i]->m_fineLeftModel );
		m_oscKnobs[i].m_fineRightKnob->setModel(
					&t->m_osc[i]->m_fineRightModel );
		m_oscKnobs[i].m_phaseOffsetKnob->setModel(
					&t->m_osc[i]->m_phaseOffsetModel );
		m_oscKnobs[i].m_stereoPhaseDetuningKnob->setModel(
				&t->m_osc[i]->m_stereoPhaseDetuningModel );
		m_oscKnobs[i].m_waveShapeBtnGrp->setModel(
					&t->m_osc[i]->m_waveShapeModel );
		m_oscKnobs[i].m_multiBandWaveTableButton->setModel(
					&t->m_osc[i]->m_useWaveTableModel );

		connect( m_oscKnobs[i].m_userWaveButton,
						SIGNAL( doubleClicked() ),
				t->m_osc[i], SLOT( oscUserDefWaveDblClick() ) );
	}

	m_unisonVoices->setModel(&t->m_unisonVoicesModel);
	m_unisonDetune->setModel(&t->m_unisonDetuneModel);
	m_unisonSpread->setModel(&t->m_unisonSpreadModel);
}


} // namespace gui


extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main( Model* model, void * )
{
	return new TripleOscillator( static_cast<InstrumentTrack *>( model ) );
}

}



} // namespace lmms
