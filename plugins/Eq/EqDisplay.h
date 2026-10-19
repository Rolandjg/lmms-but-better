/*
 * EqDisplay.h - definition of the EqDisplay class, the equalizer's curve editor
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

#ifndef EQDISPLAY_H
#define EQDISPLAY_H

#include <array>
#include <vector>

#include <QColor>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QWidget>
#include "WheelAccumulator.h"

namespace lmms
{

class BoolModel;
class EqAnalyser;
class EqControls;
class FloatModel;
class IntModel;

namespace gui
{

//! Filter shape of a band. The equalizer gives every band a fixed shape.
enum class EqBandType
{
	HighPass,
	LowShelf,
	Peak,
	HighShelf,
	LowPass
};


//! One band as the display and the dialog below it see it: a shape plus the models it edits
struct EqBand
{
	EqBandType type = EqBandType::Peak;
	QString name;
	QColor color;
	FloatModel* freq = nullptr;
	FloatModel* gain = nullptr; //!< nullptr for the pass filters, which only cut
	FloatModel* q = nullptr;    //!< bandwidth in octaves for the peaks, resonance for the other shapes
	BoolModel* active = nullptr;
	IntModel* slope = nullptr;  //!< 12/24/48 dB per octave, pass filters only

	bool hasGain() const { return gain != nullptr; }
	bool isPass() const { return type == EqBandType::HighPass || type == EqBandType::LowPass; }
};


//! The equalizer itself: spectrum, grid, filter response and one draggable node per band.
//!
//! Everything can be set by dragging the nodes, so the controls below only have to mirror the
//! selected band. The response drawn is the sum of the active bands computed from the same
//! coefficients the audio path uses, so the curve is what is heard.
class EqDisplay : public QWidget
{
	Q_OBJECT
public:
	static constexpr int BandCount = 8;

	EqDisplay(EqControls* controls, QWidget* parent);

	const EqBand& band(int index) const { return m_bands[index]; }
	int selectedBand() const { return m_selected; }

	QSize sizeHint() const override { return QSize(700, 320); }
	QSize minimumSizeHint() const override { return QSize(380, 180); }

public slots:
	void selectBand(int band);

signals:
	void bandSelected(int band);

protected:
	void paintEvent(QPaintEvent*) override;
	void resizeEvent(QResizeEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseMoveEvent(QMouseEvent*) override;
	void mouseReleaseEvent(QMouseEvent*) override;
	void mouseDoubleClickEvent(QMouseEvent*) override;
	void wheelEvent(QWheelEvent*) override;
	void leaveEvent(QEvent*) override;
	void keyPressEvent(QKeyEvent*) override;
	void contextMenuEvent(QContextMenuEvent*) override;

private slots:
	//! Reads the analysers and repaints; driven by the main window's timer
	void refresh();
	//! A band model changed, so the response has to be computed again
	void invalidateCurves();

private:
	//! Which axis a drag is allowed to change
	enum class DragAxis
	{
		Both,
		Frequency,
		Gain
	};

	float freqToX(float freq) const;
	float xToFreq(float x) const;
	float gainToY(float gainDb) const;
	float yToGain(float y) const;
	QPointF nodePos(int band) const;
	int bandAt(const QPoint& pos) const;
	//! Response of one band alone, in dB, including its slope
	float bandResponseDb(int band, float freq) const;
	//! Band that a click on empty space should switch on, preferring the peaks
	int freeBandFor(float freq) const;

	void ensureCurves();
	void buildCurves();
	void updateBars(EqAnalyser* analyser, std::vector<float>& bars, bool enabled);

	void paintGrid(QPainter& p) const;
	void paintScale(QPainter& p) const;
	void paintBars(QPainter& p, const std::vector<float>& bars, const QColor& color, bool outline) const;
	void paintCurves(QPainter& p) const;
	void paintNodes(QPainter& p) const;
	void paintReadout(QPainter& p, int band) const;

	void setBandActive(int band, bool active);
	void adjustFreq(int band, int steps, bool fine);
	void adjustGain(int band, int steps, bool fine);
	void adjustQ(int band, int steps, bool fine);
	void resetBand(int band);
	QString bandSummary(int band) const;
	void updateAccessibleText();
	void announce(int band);

	EqControls* m_controls;
	std::array<EqBand, BandCount> m_bands;

	QRectF m_plot;
	float m_dbRange = 18.f; //!< half of the visible gain range, zoomed with the wheel

	std::array<QPainterPath, BandCount> m_bandPaths;
	QPainterPath m_sumPath;
	QPainterPath m_sumFill;
	//! Coefficients of every band, cached between repaints
	std::array<std::array<double, 5>, BandCount> m_coeffs{};
	std::array<int, BandCount> m_cascades{};
	bool m_curvesDirty = true;
	int m_curveSampleRate = 0;

	std::vector<float> m_inBars;  //!< spectrum height per pixel column, 0..1
	std::vector<float> m_outBars;
	std::vector<float> m_barScratch;

	int m_selected = 2;
	int m_hovered = -1;
	int m_dragging = -1;
	int m_soloBand = -1;
	QPoint m_dragStart;
	float m_dragFreq = 0.f;
	float m_dragGain = 0.f;
	bool m_dragMoved = false;
	DragAxis m_dragAxis = DragAxis::Both;

private:
	WheelAccumulator m_wheelSteps;
};


} // namespace gui

} // namespace lmms

#endif // EQDISPLAY_H
