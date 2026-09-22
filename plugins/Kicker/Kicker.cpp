/*
 * Kicker.cpp - drum synthesizer
 *
 * Copyright (c) 2006-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2014 grejppi <grejppi/at/gmail.com>
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

#include "Kicker.h"

#include <QDomElement>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

#include "AudioEngine.h"
#include "Engine.h"
#include "FontHelper.h"
#include "InstrumentTrack.h"
#include "Knob.h"
#include "LedCheckBox.h"
#include "NotePlayHandle.h"
#include "KickerOsc.h"
#include "TempoSyncKnob.h"

#include "embed.h"
#include "plugin_export.h"

namespace lmms
{


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT kicker_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"Kicker",
	QT_TRANSLATE_NOOP( "PluginBrowser",
				"Versatile drum synthesizer" ),
	"Tobias Doerffel <tobydox/at/users.sf.net>",
	0x0100,
	Plugin::Type::Instrument,
	new PluginPixmapLoader( "logo" ),
	nullptr,
	nullptr,
} ;

}


KickerInstrument::KickerInstrument( InstrumentTrack * _instrument_track ) :
	Instrument(_instrument_track, &kicker_plugin_descriptor, nullptr, Flag::IsNotBendable),
	m_startFreqModel( 150.0f, 5.0f, 1000.0f, 1.0f, this, tr( "Start frequency" ) ),
	m_endFreqModel( 40.0f, 5.0f, 1000.0f, 1.0f, this, tr( "End frequency" ) ),
	m_decayModel( 440.0f, 5.0f, 5000.0f, 1.0f, 5000.0f, this, tr( "Length" ) ),
	m_distModel( 0.8f, 0.0f, 100.0f, 0.1f, this, tr( "Start distortion" ) ),
	m_distEndModel( 0.8f, 0.0f, 100.0f, 0.1f, this, tr( "End distortion" ) ),
	m_gainModel( 1.0f, 0.1f, 5.0f, 0.05f, this, tr( "Gain" ) ),
	m_envModel( 0.163f, 0.01f, 1.0f, 0.001f, this, tr( "Envelope slope" ) ),
	m_noiseModel( 0.0f, 0.0f, 1.0f, 0.01f, this, tr( "Noise" ) ),
	m_clickModel( 0.4f, 0.0f, 1.0f, 0.05f, this, tr( "Click" ) ),
	m_slopeModel( 0.06f, 0.001f, 1.0f, 0.001f, this, tr( "Frequency slope" ) ),
	m_startNoteModel( true, this, tr( "Start from note" ) ),
	m_endNoteModel( false, this, tr( "End to note" ) ),
	m_versionModel( KICKER_PRESET_VERSION, 0, KICKER_PRESET_VERSION, this, "" )
{
}




void KickerInstrument::saveSettings(QDomDocument& doc, QDomElement& elem)
{
	m_startFreqModel.saveSettings(doc, elem, "startfreq");
	m_endFreqModel.saveSettings(doc, elem, "endfreq");
	m_decayModel.saveSettings(doc, elem, "decay");
	m_distModel.saveSettings(doc, elem, "dist");
	m_distEndModel.saveSettings(doc, elem, "distend");
	m_gainModel.saveSettings(doc, elem, "gain");
	m_envModel.saveSettings(doc, elem, "env");
	m_noiseModel.saveSettings(doc, elem, "noise");
	m_clickModel.saveSettings(doc, elem, "click");
	m_slopeModel.saveSettings(doc, elem, "slope");
	m_startNoteModel.saveSettings(doc, elem, "startnote");
	m_endNoteModel.saveSettings(doc, elem, "endnote");
	m_versionModel.saveSettings(doc, elem, "version");
}




void KickerInstrument::loadSettings(const QDomElement& elem)
{
	m_versionModel.loadSettings(elem, "version");

	m_startFreqModel.loadSettings(elem, "startfreq");
	m_endFreqModel.loadSettings(elem, "endfreq");
	m_decayModel.loadSettings(elem, "decay");
	m_distModel.loadSettings(elem, "dist");
	if (elem.hasAttribute("distend") || !elem.firstChildElement("distend").isNull())
	{
		m_distEndModel.loadSettings(elem, "distend");
	}
	else
	{
		m_distEndModel.setValue(m_distModel.value());
	}
	m_gainModel.loadSettings(elem, "gain");
	m_envModel.loadSettings(elem, "env");
	m_noiseModel.loadSettings(elem, "noise");
	m_clickModel.loadSettings(elem, "click");
	m_slopeModel.loadSettings(elem, "slope");
	m_startNoteModel.loadSettings(elem, "startnote");
	if (m_versionModel.value() < 1)
	{
		m_startNoteModel.setValue(false);
	}
	m_endNoteModel.loadSettings(elem, "endnote");

	// Try to maintain backwards compatibility
	if (!elem.hasAttribute("version"))
	{
		m_startNoteModel.setValue(false);
		m_decayModel.setValue(m_decayModel.value() * 1.33f);
		m_envModel.setValue(1.0f);
		m_slopeModel.setValue(1.0f);
		m_clickModel.setValue(0.0f);
	}
	m_versionModel.setValue(KICKER_PRESET_VERSION);
}




QString KickerInstrument::nodeName() const
{
	return kicker_plugin_descriptor.name;
}

using DistFX = DspEffectLibrary::Distortion;
using SweepOsc = KickerOsc<DspEffectLibrary::MonoToStereoAdaptor<DistFX>>;

void KickerInstrument::playNote( NotePlayHandle * _n,
						SampleFrame* _working_buffer )
{
	const f_cnt_t frames = _n->framesLeftForCurrentPeriod();
	const f_cnt_t offset = _n->noteOffset();
	const float decfr = m_decayModel.value() * Engine::audioEngine()->outputSampleRate() / 1000.0f;
	const f_cnt_t tfp = _n->totalFramesPlayed();

	if (!_n->m_pluginData)
	{
		_n->m_pluginData = new SweepOsc(
					DistFX( m_distModel.value(),
							m_gainModel.value() ),
					m_startNoteModel.value() ? _n->frequency() : m_startFreqModel.value(),
					m_endNoteModel.value() ? _n->frequency() : m_endFreqModel.value(),
					m_noiseModel.value() * m_noiseModel.value(),
					m_clickModel.value() * 0.25f,
					m_slopeModel.value(),
					m_envModel.value(),
					m_distModel.value(),
					m_distEndModel.value(),
					decfr );
	}
	else if( tfp > decfr && !_n->isReleased() )
	{
		_n->noteOff();
	}

	auto so = static_cast<SweepOsc*>(_n->m_pluginData);
	so->update( _working_buffer + offset, frames, Engine::audioEngine()->outputSampleRate() );

	if( _n->isReleased() )
	{
		// We need this to check if the release has ended
		const float desired = desiredReleaseFrames();

		// This can be considered the current release frame in the "global" context of the release.
		// We need it with the desired number of release frames to compute the linear decay.
		f_cnt_t currentReleaseFrame = _n->releaseFramesDone();

		// Start applying the release at the correct frame
		const float framesBeforeRelease = _n->framesBeforeRelease();
		for (f_cnt_t f = framesBeforeRelease; f < frames; ++f, ++currentReleaseFrame)
		{
			const bool releaseStillActive = currentReleaseFrame < desired;
			const float attenuation = releaseStillActive ? (1.0f - (currentReleaseFrame / desired)) : 0.f;

			_working_buffer[f + offset][0] *= attenuation;
			_working_buffer[f + offset][1] *= attenuation;
		}
	}
}




float KickerInstrument::previewStartFrequency() const
{
	return m_startNoteModel.value() ? instrumentTrack()->baseFreq() : m_startFreqModel.value();
}




float KickerInstrument::previewEndFrequency() const
{
	return m_endNoteModel.value() ? instrumentTrack()->baseFreq() : m_endFreqModel.value();
}




std::vector<float> KickerInstrument::renderPreview(float sampleRate) const
{
	const float length = m_decayModel.value() * sampleRate / 1000.0f;
	SweepOsc osc(DistFX(m_distModel.value(), m_gainModel.value()),
		previewStartFrequency(), previewEndFrequency(),
		m_noiseModel.value() * m_noiseModel.value(), m_clickModel.value() * 0.25f,
		m_slopeModel.value(), m_envModel.value(), m_distModel.value(), m_distEndModel.value(), length);

	std::vector<SampleFrame> frames(static_cast<std::size_t>(length) + 1);
	osc.update(frames.data(), frames.size(), sampleRate);
	std::vector<float> mono(frames.size());
	for (std::size_t i = 0; i < frames.size(); ++i) { mono[i] = frames[i].left(); }
	return mono;
}




void KickerInstrument::deleteNotePluginData( NotePlayHandle * _n )
{
	delete static_cast<SweepOsc *>( _n->m_pluginData );
}




gui::PluginView * KickerInstrument::instantiateView( QWidget * _parent )
{
	return new gui::KickerInstrumentView( this, _parent );
}


namespace gui
{


class KickerKnob : public Knob
{
public:
	KickerKnob( QWidget * _parent ) :
			Knob( KnobType::Styled, _parent )
	{
		setFixedSize( 29, 29 );
		setObjectName( "smallKnob" );
	}
};


class KickerEnvKnob : public TempoSyncKnob
{
public:
	KickerEnvKnob( QWidget * _parent ) :
			TempoSyncKnob( KnobType::Styled, _parent )
	{
		setFixedSize( 29, 29 );
		setObjectName( "smallKnob" );
	}
};


class KickerLargeKnob : public Knob
{
public:
	KickerLargeKnob( QWidget * _parent ) :
			Knob( KnobType::Styled, _parent )
	{
		setFixedSize( 34, 34 );
		setObjectName( "largeKnob" );
	}
};




namespace
{

constexpr int PreviewTop = 176;
const QColor KickerBlue(81, 159, 255);

//! Engraved screen showing the rendered kick (blue) and its pitch sweep (pale line)
class KickerPreview : public QWidget
{
public:
	KickerPreview(KickerInstrument* kicker, QWidget* parent) :
		QWidget(parent),
		m_kicker(kicker)
	{
		setToolTip(QObject::tr("Preview of one kick at the track's base note: waveform and pitch sweep"));
	}

	void invalidate()
	{
		m_dirty = true;
		update();
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		if (m_dirty)
		{
			// Rendering a few hundred ms of a sine sweep is cheap enough to do on demand
			m_samples = m_kicker->renderPreview(PreviewRate);
			m_dirty = false;
		}

		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF outer = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

		// Engraved bevel like the panels of the artwork: dark top-left, light bottom-right
		p.setPen(QPen(QColor(90, 94, 98), 1));
		p.drawLine(outer.topLeft(), outer.topRight());
		p.drawLine(outer.topLeft(), outer.bottomLeft());
		p.setPen(QPen(QColor(235, 237, 240), 1));
		p.drawLine(outer.bottomLeft(), outer.bottomRight());
		p.drawLine(outer.topRight(), outer.bottomRight());

		const QRectF screen = outer.adjusted(1.5, 1.5, -1.5, -1.5);
		QLinearGradient bg(0, screen.top(), 0, screen.bottom());
		bg.setColorAt(0, QColor(12, 16, 22));
		bg.setColorAt(1, QColor(26, 33, 42));
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		p.drawRoundedRect(screen, 3, 3);

		const double mid = screen.center().y();
		const double half = screen.height() / 2.0 - 4.0;
		p.setPen(QPen(QColor(255, 255, 255, 25), 1));
		p.drawLine(QPointF(screen.left() + 3, mid), QPointF(screen.right() - 3, mid));

		const auto count = m_samples.size();
		if (count > 1)
		{
			// Min/max per pixel column keeps even long, dense kicks readable
			const int columns = static_cast<int>(screen.width()) - 6;
			QPainterPath wave;
			std::vector<QPointF> lower;
			for (int x = 0; x < columns; ++x)
			{
				const auto from = count * x / columns;
				const auto to = std::max(from + 1, count * (x + 1) / columns);
				float lo = 1.f, hi = -1.f;
				for (auto i = from; i < to && i < count; ++i)
				{
					lo = std::min(lo, m_samples[i]);
					hi = std::max(hi, m_samples[i]);
				}
				const double px = screen.left() + 3 + x;
				const QPointF top(px, mid - std::clamp(hi, -1.f, 1.f) * half);
				if (x == 0) { wave.moveTo(top); } else { wave.lineTo(top); }
				lower.emplace_back(px, mid - std::clamp(lo, -1.f, 1.f) * half);
			}
			for (auto it = lower.rbegin(); it != lower.rend(); ++it) { wave.lineTo(*it); }
			wave.closeSubpath();

			QColor fill = KickerBlue;
			fill.setAlpha(170);
			p.setBrush(fill);
			p.setPen(QPen(KickerBlue.lighter(130), 0.8));
			p.drawPath(wave);
		}

		// Pitch sweep on a log scale from 20 Hz to 1 kHz, following the oscillator's formula
		const float startFreq = m_kicker->previewStartFrequency();
		const float endFreq = m_kicker->previewEndFrequency();
		const float slope = m_kicker->frequencySlope();
		auto freqToY = [&](float f)
		{
			const float norm = std::log(std::clamp(f, 20.f, 1000.f) / 20.f) / std::log(50.f);
			return screen.bottom() - 4 - norm * (screen.height() - 8);
		};
		QPainterPath pitch;
		const int steps = 80;
		for (int i = 0; i <= steps; ++i)
		{
			const float t = static_cast<float>(i) / steps;
			const float f = endFreq + (startFreq - endFreq) * (1.f - std::pow(t, slope));
			const QPointF pt(screen.left() + 3 + t * (screen.width() - 6), freqToY(f));
			if (i == 0) { pitch.moveTo(pt); } else { pitch.lineTo(pt); }
		}
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(QColor(230, 240, 255, 200), 1.2, Qt::DashLine));
		p.drawPath(pitch);

		p.setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
		p.setPen(QColor(170, 205, 255));
		p.drawText(QRectF(screen.left() + 6, screen.top() + 3, 140, 12), Qt::AlignLeft | Qt::AlignVCenter,
			QObject::tr("%1 Hz → %2 Hz").arg(qRound(startFreq)).arg(qRound(endFreq)));
		p.drawText(QRectF(screen.right() - 86, screen.bottom() - 15, 80, 12), Qt::AlignRight | Qt::AlignVCenter,
			QObject::tr("%1 ms").arg(qRound(m_kicker->lengthMs())));
	}

private:
	static constexpr float PreviewRate = 22050.f;

	KickerInstrument* m_kicker;
	std::vector<float> m_samples;
	bool m_dirty = true;
};

} // namespace




void KickerInstrumentView::paintEvent(QPaintEvent*)
{
	// The original artwork is split just above the logo panel to make room for the preview,
	// and the gap is filled with the brushed metal in between the panels
	static const auto artwork = PLUGIN_NAME::getIconPixmap("artwork");
	QPainter p(this);
	p.drawPixmap(0, 0, artwork, 0, 0, 250, PreviewTop);
	p.drawPixmap(QRect(0, PreviewTop, 250, PreviewHeight), artwork, QRect(0, PreviewTop, 250, 2));
	p.drawPixmap(0, PreviewTop + PreviewHeight, artwork, 0, PreviewTop, 250, 250 - PreviewTop);
}




KickerInstrumentView::KickerInstrumentView( Instrument * _instrument,
							QWidget * _parent ) :
	InstrumentView( _instrument, _parent )
{
	setFixedSize(sizeHint());
	m_preview = new KickerPreview(castModel<KickerInstrument>(), this);
	m_preview->setGeometry(9, PreviewTop + 2, 232, PreviewHeight - 4);

	const int ROW1 = 14;
	const int ROW2 = ROW1 + 56;
	const int ROW3 = ROW2 + 56;
	const int LED_ROW = 63;
	const int COL1 = 14;
	const int COL2 = COL1 + 56;
	const int COL3 = COL2 + 56;
	const int COL4 = COL3 + 41;
	const int COL5 = COL4 + 41;
	const int END_COL = COL1 + 48;
	
	m_startFreqKnob = new KickerLargeKnob( this );
	m_startFreqKnob->setHintText( tr( "Start frequency:" ), "Hz" );
	m_startFreqKnob->move( COL1, ROW1 );

	m_endFreqKnob = new KickerLargeKnob( this );
	m_endFreqKnob->setHintText( tr( "End frequency:" ), "Hz" );
	m_endFreqKnob->move( END_COL, ROW1 );

	m_slopeKnob = new KickerKnob( this );
	m_slopeKnob->setHintText( tr( "Frequency slope:" ), "" );
	m_slopeKnob->move( COL3, ROW1 );

	m_gainKnob = new KickerKnob( this );
	m_gainKnob->setHintText( tr( "Gain:" ), "" );
	m_gainKnob->move( COL1, ROW3 );

	m_decayKnob = new KickerEnvKnob( this );
	m_decayKnob->setHintText( tr( "Envelope length:" ), "ms" );
	m_decayKnob->move( COL2, ROW3 );

	m_envKnob = new KickerKnob( this );
	m_envKnob->setHintText( tr( "Envelope slope:" ), "" );
	m_envKnob->move( COL3, ROW3 );

	m_clickKnob = new KickerKnob( this );
	m_clickKnob->setHintText( tr( "Click:" ), "" );
	m_clickKnob->move( COL5, ROW1 );

	m_noiseKnob = new KickerKnob( this );
	m_noiseKnob->setHintText( tr( "Noise:" ), "" );
	m_noiseKnob->move( COL5, ROW3 );

	m_distKnob = new KickerKnob( this );
	m_distKnob->setHintText( tr( "Start distortion:" ), "" );
	m_distKnob->move( COL4, ROW2 );

	m_distEndKnob = new KickerKnob( this );
	m_distEndKnob->setHintText( tr( "End distortion:" ), "" );
	m_distEndKnob->move( COL5, ROW2 );

	m_startNoteToggle = new LedCheckBox( "", this, "", LedCheckBox::LedColor::Green );
	m_startNoteToggle->move( COL1 + 8, LED_ROW );

	m_endNoteToggle = new LedCheckBox( "", this, "", LedCheckBox::LedColor::Green );
	m_endNoteToggle->move( END_COL + 8, LED_ROW );

}




void KickerInstrumentView::modelChanged()
{
	auto k = castModel<KickerInstrument>();
	m_startFreqKnob->setModel( &k->m_startFreqModel );
	m_endFreqKnob->setModel( &k->m_endFreqModel );
	m_decayKnob->setModel( &k->m_decayModel );
	m_distKnob->setModel( &k->m_distModel );
	m_distEndKnob->setModel( &k->m_distEndModel );
	m_gainKnob->setModel( &k->m_gainModel );
	m_envKnob->setModel( &k->m_envModel );
	m_noiseKnob->setModel( &k->m_noiseModel );
	m_clickKnob->setModel( &k->m_clickModel );
	m_slopeKnob->setModel( &k->m_slopeModel );
	m_startNoteToggle->setModel( &k->m_startNoteModel );
	m_endNoteToggle->setModel( &k->m_endNoteModel );

	auto preview = static_cast<KickerPreview*>(m_preview);
	for (Model* model : std::initializer_list<Model*>{&k->m_startFreqModel, &k->m_endFreqModel, &k->m_decayModel,
		&k->m_distModel, &k->m_distEndModel, &k->m_gainModel, &k->m_envModel, &k->m_noiseModel, &k->m_clickModel,
		&k->m_slopeModel, &k->m_startNoteModel, &k->m_endNoteModel})
	{
		connect(model, &Model::dataChanged, preview, [preview] { preview->invalidate(); });
	}
	preview->invalidate();
}


} // namespace gui


extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main( Model * m, void * )
{
	return new KickerInstrument( static_cast<InstrumentTrack *>( m ) );
}


}


} // namespace lmms
