/*
 * ModernWidgets.h - vector-drawn building blocks for modernized plugin UIs
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

#ifndef LMMS_GUI_MODERN_WIDGETS_H
#define LMMS_GUI_MODERN_WIDGETS_H

#include <QColor>
#include <QWidget>
#include <array>
#include <atomic>
#include <functional>
#include <QStringList>

#include "AutomatableButton.h"
#include "AutomatableModelView.h"
#include "lmms_export.h"
#include "WheelAccumulator.h"

class QBoxLayout;
class QGridLayout;
class QPainter;

namespace lmms
{

class ComboBoxModel;
class FloatModel;
class graphModel;
class SampleFrame;


//! Lock-free ring of recent samples that the audio thread fills and a scope widget reads.
//! Every sample is an atomic (relaxed loads/stores compile to plain moves), so concurrent access is
//! well-defined; a reader may still see a mix of older and newer samples, which is fine for a display.
class ScopeBuffer
{
public:
	static constexpr std::size_t Size = 1024;

	void push(float left, float right)
	{
		const auto i = m_write.load(std::memory_order_relaxed);
		m_left[i].store(left, std::memory_order_relaxed);
		m_right[i].store(right, std::memory_order_relaxed);
		m_write.store((i + 1) % Size, std::memory_order_release);
	}

	std::size_t writePosition() const { return m_write.load(std::memory_order_acquire); }
	float left(std::size_t i) const { return m_left[i % Size].load(std::memory_order_relaxed); }
	float right(std::size_t i) const { return m_right[i % Size].load(std::memory_order_relaxed); }

private:
	std::array<std::atomic<float>, Size> m_left{};
	std::array<std::atomic<float>, Size> m_right{};
	std::atomic<std::size_t> m_write{0};
};

namespace gui
{

class Knob;

//! Shared palette and painting helpers so all modernized plugins look like one family
namespace modern
{

// Colors come from the theme (lmms--gui--ModernPalette in style.css, see ModernPalette below) so these
// plugins sit next to the native ones and follow other or high-contrast themes
LMMS_EXPORT QColor windowColor();       //!< panel background
LMMS_EXPORT QColor sectionColor();      //!< recessed panel
LMMS_EXPORT QColor sectionBorder();     //!< hairline
LMMS_EXPORT QColor displayColor();      //!< graph screens
LMMS_EXPORT QColor gridColor();
LMMS_EXPORT QColor textColor();
LMMS_EXPORT QColor dimTextColor();
LMMS_EXPORT QColor buttonColor();
LMMS_EXPORT QColor activeButtonColor(); //!< selected item

//! The LMMS green, used for values, curves and active states
LMMS_EXPORT QColor accentColor();
//! Secondary trace color where a display has to tell two signals apart (e.g. right channel)
LMMS_EXPORT QColor secondaryColor();
//! Highlight for exceptional states such as freeze
LMMS_EXPORT QColor warningColor();

//! Give a plugin dialog the flat dark background shared by all modern plugins
LMMS_EXPORT void applyWindowStyle(QWidget* widget);

//! Paint the dark "screen" used behind curves, waveforms and graphs
LMMS_EXPORT void paintDisplay(QPainter& p, const QRectF& rect, int verticalLines = 8, int horizontalLines = 4);

//! Create a modern knob bound to @p model
LMMS_EXPORT Knob* makeKnob(QWidget* parent, FloatModel* model, const QString& label,
	const QString& hint, const QString& unit);

} // namespace modern


//! The modern:: colors. Defaults are the default theme's.
struct LMMS_EXPORT ModernColors
{
	QColor window{29, 32, 37};
	QColor section{23, 26, 30};
	QColor sectionBorder{10, 11, 13};
	QColor display{14, 16, 19};
	QColor grid{255, 255, 255, 22};
	QColor text{220, 226, 234};
	QColor dimText{141, 150, 163};
	QColor button{42, 47, 54};
	QColor activeButton{23, 61, 43};
	QColor accent{29, 226, 118};
	QColor secondary{64, 196, 214};
	QColor warning{214, 236, 82};

	//! The current theme's colors. Until the GUI and its style sheet exist (headless, or static
	//! initializers in plugins) this returns the defaults.
	static const ModernColors& current();
};


//! Theme hook for the modern:: colors: style sheets set them with qproperty-<name> on
//! lmms--gui--ModernPalette, like LmmsPalette. Unset properties keep the defaults.
class LMMS_EXPORT ModernPalette : public QWidget
{
	Q_OBJECT
	Q_PROPERTY(QColor window MEMBER window)
	Q_PROPERTY(QColor section MEMBER section)
	Q_PROPERTY(QColor sectionBorder MEMBER sectionBorder)
	Q_PROPERTY(QColor display MEMBER display)
	Q_PROPERTY(QColor grid MEMBER grid)
	Q_PROPERTY(QColor text MEMBER text)
	Q_PROPERTY(QColor dimText MEMBER dimText)
	Q_PROPERTY(QColor button MEMBER button)
	Q_PROPERTY(QColor activeButton MEMBER activeButton)
	Q_PROPERTY(QColor accent MEMBER accent)
	Q_PROPERTY(QColor secondary MEMBER secondary)
	Q_PROPERTY(QColor warning MEMBER warning)
public:
	//! Reads the style sheet's colors; needs a QApplication
	ModernPalette();

	ModernColors colors() const;

private:
	QColor window, section, sectionBorder, display, grid, text, dimText, button, activeButton, accent,
		secondary, warning;
};


//! A recessed panel with a small caps title, like the group boxes of native effect dialogs. Put child widgets into layout().
class LMMS_EXPORT ModernSection : public QWidget
{
	Q_OBJECT
public:
	ModernSection(const QString& title, QWidget* parent = nullptr);

	QGridLayout* grid() const { return m_grid; }

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent*) override;

private:
	QFont titleFont() const;

	QString m_title;
	QGridLayout* m_grid;
};


//! Flat checkable button bound to a BoolModel; green text on dark green when active
class LMMS_EXPORT ModernToggle : public AutomatableButton
{
	Q_OBJECT
public:
	ModernToggle(const QString& text, QWidget* parent);

	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent*) override;
};


//! Unboxed section title: small bold caps followed by a hairline. When @p toggle is set it also
//! switches a BoolModel on and off, with a square LED in front of the title.
class LMMS_EXPORT ModernHeader : public AutomatableButton
{
	Q_OBJECT
public:
	ModernHeader(const QString& title, QWidget* parent, bool toggle = false);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override { return sizeHint(); }

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent*) override;

private:
	QFont titleFont() const;

	bool m_toggle;
};


//! Horizontal segmented selector for an IntModel - every option is visible and one click away.
//! Labels come from a ComboBoxModel automatically, or from setLabels(); setItemPainter() allows
//! drawing glyphs instead of text.
class LMMS_EXPORT ModernSegmented : public QWidget, public IntModelView
{
	Q_OBJECT
public:
	using ItemPainter = std::function<void(QPainter& p, const QRectF& rect, int index, const QColor& color)>;

	explicit ModernSegmented(QWidget* parent);

	void setLabels(const QStringList& labels) { m_labels = labels; updateGeometry(); update(); }
	void setToolTips(const QStringList& tips) { m_tips = tips; }
	void setItemPainter(ItemPainter painter, int itemWidth);

	//! Flat: theme buttons (default). Outline: black buttons with a thin light border, as used by
	//! the older bitmap-skinned effects such as WaveShaper and DynamicsProcessor
	enum class Style { Flat, Outline };
	void setStyle(Style style) { m_style = style; update(); }

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override { return sizeHint(); }

	void modelChanged() override;

signals:
	void itemDoubleClicked(int index);

protected:
	bool event(QEvent* e) override;
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseDoubleClickEvent(QMouseEvent*) override;
	void wheelEvent(QWheelEvent*) override;
	void contextMenuEvent(QContextMenuEvent*) override;

private:
	int count() const;
	QString label(int index) const;
	int indexAt(int x) const;

	QStringList m_labels;
	QStringList m_tips;
	ItemPainter m_itemPainter;
	int m_itemWidth = 0;
	Style m_style = Style::Flat;

private:
	WheelAccumulator m_wheelSteps;
};


//! Stereo peak meter that reads two linear peak values written by the audio thread
class LMMS_EXPORT ModernMeter : public QWidget
{
	Q_OBJECT
public:
	ModernMeter(QWidget* parent, const float* peakLeft, const float* peakRight);

	QSize sizeHint() const override { return QSize(14, 120); }

protected:
	void paintEvent(QPaintEvent*) override;

private slots:
	void refresh();

private:
	const float* m_peakLeft;
	const float* m_peakRight;
	float m_displayLeft = 0.f;
	float m_displayRight = 0.f;
	float m_holdLeft = 0.f;
	float m_holdRight = 0.f;
	int m_holdCounterLeft = 0;
	int m_holdCounterRight = 0;
};


//! Goniometer (mid up, side sideways) with a phase correlation bar
class LMMS_EXPORT ModernStereoScope : public QWidget
{
	Q_OBJECT
public:
	ModernStereoScope(QWidget* parent, const ScopeBuffer* buffer);
	QSize sizeHint() const override { return QSize(128, 128); }

protected:
	void paintEvent(QPaintEvent*) override;

private:
	const ScopeBuffer* m_buffer;
	float m_correlation = 0.f;
};


//! Transparent overlay for a transfer-curve Graph: shows where the current signal level sits on
//! the curve with a vertical line and a glowing dot. The level (linear, 0..1 across the graph)
//! is written by the audio thread.
class LMMS_EXPORT GraphLevelIndicator : public QWidget
{
	Q_OBJECT
public:
	GraphLevelIndicator(QWidget* parent, const graphModel* curve, const std::atomic<float>* level, const QColor& color);

protected:
	void paintEvent(QPaintEvent*) override;

private:
	const graphModel* m_curve;
	const std::atomic<float>* m_level;
	QColor m_color;
	float m_display = 0.f;
};


} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_MODERN_WIDGETS_H
