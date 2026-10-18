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

#include <QBoxLayout>
#include <QDomElement>
#include <QGridLayout>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

#include "AudioEngine.h"
#include "Engine.h"
#include "FontHelper.h"
#include "InstrumentTrack.h"
#include "Knob.h"
#include "ModernWidgets.h"
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


namespace
{

//! Screen showing the rendered kick and its pitch sweep (dashed line)
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
		modern::paintDisplay(p, rect(), 8, 0);
		const QRectF screen = QRectF(rect()).adjusted(1, 1, -1, -1);

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

			QColor fill = modern::accentColor();
			fill.setAlpha(110);
			p.setBrush(fill);
			p.setPen(QPen(modern::accentColor(), 0.8));
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
		p.setPen(QPen(QColor(220, 226, 234, 190), 1.2, Qt::DashLine));
		p.drawPath(pitch);

		p.setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
		p.setPen(modern::dimTextColor());
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




KickerInstrumentView::KickerInstrumentView( Instrument * _instrument,
							QWidget * _parent ) :
	InstrumentView( _instrument, _parent )
{
	modern::applyWindowStyle(this);

	auto knob = [this](const QString& label, const QString& hint, const QString& unit, const QString& tip)
	{
		auto k = new Knob(KnobType::Modern, label, this);
		k->setHintText(hint, unit);
		k->setToolTip(tip);
		return k;
	};
	// A titled group of controls laid out in a grid
	auto group = [this](const QString& title, QGridLayout*& grid)
	{
		auto layout = new QVBoxLayout();
		layout->setSpacing(3);
		layout->addWidget(new ModernHeader(title, this));
		grid = new QGridLayout();
		grid->setHorizontalSpacing(2);
		grid->setVerticalSpacing(3);
		grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
		layout->addLayout(grid);
		return layout;
	};

	auto mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(6, 6, 6, 6);
	mainLayout->setSpacing(8);

	m_preview = new KickerPreview(castModel<KickerInstrument>(), this);
	m_preview->setFixedHeight(76);
	mainLayout->addWidget(m_preview);

	m_startFreqKnob = knob(tr("START"), tr("Start frequency:"), "Hz", tr("Pitch at the start of the kick"));
	m_endFreqKnob = knob(tr("END"), tr("End frequency:"), "Hz", tr("Pitch the kick falls to"));
	m_slopeKnob = knob(tr("SLOPE"), tr("Frequency slope:"), "", tr("How quickly the pitch falls"));
	m_startNoteToggle = new ModernToggle(tr("NOTE"), this);
	m_startNoteToggle->setToolTip(tr("Start at the pitch of the played note"));
	m_endNoteToggle = new ModernToggle(tr("NOTE"), this);
	m_endNoteToggle->setToolTip(tr("End at the pitch of the played note"));

	m_clickKnob = knob(tr("CLICK"), tr("Click:"), "", tr("Snap at the very start"));
	m_noiseKnob = knob(tr("NOISE"), tr("Noise:"), "", tr("Noise mixed into the kick"));

	m_gainKnob = knob(tr("GAIN"), tr("Gain:"), "", tr("Output level"));
	m_decayKnob = new TempoSyncKnob(KnobType::Modern, tr("LENGTH"), this);
	m_decayKnob->setHintText(tr("Envelope length:"), "ms");
	m_decayKnob->setToolTip(tr("Length of the kick (right-click to sync to tempo)"));
	m_envKnob = knob(tr("SLOPE"), tr("Envelope slope:"), "", tr("Shape of the volume decay"));

	m_distKnob = knob(tr("START"), tr("Start distortion:"), "", tr("Distortion at the start"));
	m_distEndKnob = knob(tr("END"), tr("End distortion:"), "", tr("Distortion at the end"));

	QGridLayout* grid = nullptr;
	auto topRow = new QHBoxLayout();
	topRow->setSpacing(10);
	topRow->addLayout(group(tr("PITCH"), grid), 3);
	grid->addWidget(m_startFreqKnob, 0, 0, Qt::AlignHCenter);
	grid->addWidget(m_endFreqKnob, 0, 1, Qt::AlignHCenter);
	grid->addWidget(m_slopeKnob, 0, 2, Qt::AlignHCenter);
	grid->addWidget(m_startNoteToggle, 1, 0);
	grid->addWidget(m_endNoteToggle, 1, 1);
	topRow->addLayout(group(tr("TONE"), grid), 2);
	grid->addWidget(m_clickKnob, 0, 0, Qt::AlignHCenter);
	grid->addWidget(m_noiseKnob, 0, 1, Qt::AlignHCenter);
	mainLayout->addLayout(topRow);

	auto bottomRow = new QHBoxLayout();
	bottomRow->setSpacing(10);
	bottomRow->addLayout(group(tr("ENVELOPE"), grid), 3);
	grid->addWidget(m_gainKnob, 0, 0, Qt::AlignHCenter);
	grid->addWidget(m_decayKnob, 0, 1, Qt::AlignHCenter);
	grid->addWidget(m_envKnob, 0, 2, Qt::AlignHCenter);
	bottomRow->addLayout(group(tr("DISTORTION"), grid), 2);
	grid->addWidget(m_distKnob, 0, 0, Qt::AlignHCenter);
	grid->addWidget(m_distEndKnob, 0, 1, Qt::AlignHCenter);
	mainLayout->addLayout(bottomRow);

	mainLayout->addStretch(1);
	setFixedSize(250, mainLayout->sizeHint().height());
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
