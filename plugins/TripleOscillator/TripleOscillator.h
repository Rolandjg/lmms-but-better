/*
 * TripleOscillator.h - declaration of class TripleOscillator a powerful
 *                      instrument-plugin with 3 oscillators
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

#ifndef _TRIPLE_OSCILLATOR_H
#define _TRIPLE_OSCILLATOR_H

#include <memory>

#include "Instrument.h"
#include "InstrumentView.h"
#include "AutomatableModel.h"
#include "ComboBoxModel.h"
#include "OscillatorConstants.h"
#include "SampleBuffer.h"

namespace lmms
{


class NotePlayHandle;  // IWYU pragma: keep
class Oscillator;


namespace gui
{
class AutomatableButtonGroup;
class Knob;
class LcdSpinBox;
class PixmapButton;
class TripleOscillatorView;
} // namespace gui


const int NUM_OF_OSCILLATORS = 3;


class OscillatorObject : public Model
{
	Q_OBJECT
public:
	OscillatorObject( Model * _parent, int _idx );
private:
	FloatModel m_volumeModel;
	FloatModel m_panModel;
	FloatModel m_coarseModel;
	FloatModel m_fineLeftModel;
	FloatModel m_fineRightModel;
	FloatModel m_phaseOffsetModel;
	FloatModel m_stereoPhaseDetuningModel;
	IntModel m_waveShapeModel;
	IntModel m_modulationAlgoModel;
	BoolModel m_useWaveTableModel;
	std::shared_ptr<const SampleBuffer> m_sampleBuffer = SampleBuffer::emptyBuffer();
	std::shared_ptr<const OscillatorConstants::waveform_t> m_userAntiAliasWaveTable;

	float m_volumeLeft;
	float m_volumeRight;

	// normalized detuning -> x/sampleRate
	float m_detuningLeft;
	float m_detuningRight;
	// normalized offset -> x/360
	float m_phaseOffsetLeft;
	float m_phaseOffsetRight;
	bool m_useWaveTable;

	friend class TripleOscillator;
	friend class gui::TripleOscillatorView;


private slots:
	void oscUserDefWaveDblClick();

	void updateVolume();
	void updateDetuningLeft();
	void updateDetuningRight();
	void updatePhaseOffsetLeft();
	void updatePhaseOffsetRight();
	void updateUseWaveTable();

} ;




class TripleOscillator : public Instrument
{
	Q_OBJECT
public:
	TripleOscillator( InstrumentTrack * _track );
	~TripleOscillator() override = default;

	void playNote( NotePlayHandle * _n,
						SampleFrame* _working_buffer ) override;
	void deleteNotePluginData( NotePlayHandle * _n ) override;


	void saveSettings( QDomDocument & _doc, QDomElement & _parent ) override;
	void loadSettings( const QDomElement & _this ) override;

	QString nodeName() const override;

	float desiredReleaseTimeMs() const override
	{
		return 3.f;
	}

	gui::PluginView* instantiateView( QWidget * _parent ) override;


protected slots:
	void updateAllDetuning();


private:
	OscillatorObject * m_osc[NUM_OF_OSCILLATORS];

	//! Unison: every note plays several detuned, panned copies of the whole oscillator stack
	static constexpr int MaxUnisonVoices = 8;
	ComboBoxModel m_unisonVoicesModel;
	FloatModel m_unisonDetuneModel;
	FloatModel m_unisonSpreadModel;

	struct UnisonVoice;
	struct NoteData;

	friend class gui::TripleOscillatorView;

} ;


namespace gui
{


class TripleOscillatorView : public InstrumentView
{
	Q_OBJECT
public:
	TripleOscillatorView( Instrument * _instrument, QWidget * _parent );
	~TripleOscillatorView() override = default;

	QSize sizeHint() const override { return QSize(250, UnisonStripY + UnisonStripHeight); }
	QSize minimumSizeHint() const override { return sizeHint(); }

protected:
	void paintEvent(QPaintEvent*) override;


private:
	void modelChanged() override;

	AutomatableButtonGroup * m_mod1BtnGrp;
	AutomatableButtonGroup * m_mod2BtnGrp;

	struct OscillatorKnobs
	{
		OscillatorKnobs( Knob * v,
					Knob * p,
					Knob * c,
					Knob * fl,
					Knob * fr,
					Knob * po,
					Knob * spd,
					PixmapButton * uwb,
					AutomatableButtonGroup * wsbg,
					PixmapButton * wt) :
			m_volKnob( v ),
			m_panKnob( p ),
			m_coarseKnob( c ),
			m_fineLeftKnob( fl ),
			m_fineRightKnob( fr ),
			m_phaseOffsetKnob( po ),
			m_stereoPhaseDetuningKnob( spd ),
			m_userWaveButton( uwb ),
			m_waveShapeBtnGrp( wsbg ),
			m_multiBandWaveTableButton( wt )
		{
		}
		OscillatorKnobs() = default;
		Knob * m_volKnob;
		Knob * m_panKnob;
		Knob * m_coarseKnob;
		Knob * m_fineLeftKnob;
		Knob * m_fineRightKnob;
		Knob * m_phaseOffsetKnob;
		Knob * m_stereoPhaseDetuningKnob;
		PixmapButton * m_userWaveButton;
		AutomatableButtonGroup * m_waveShapeBtnGrp;
		PixmapButton * m_multiBandWaveTableButton;

	} ;

	OscillatorKnobs m_oscKnobs[NUM_OF_OSCILLATORS];

	//! The unison controls sit on a strip below the original 250x250 artwork
	static constexpr int UnisonStripY = 250;
	static constexpr int UnisonStripHeight = 42;
	LcdSpinBox* m_unisonVoices;
	Knob* m_unisonDetune;
	Knob* m_unisonSpread;
} ;

} // namespace gui

} // namespace lmms

#endif
