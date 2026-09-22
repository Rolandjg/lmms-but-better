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
//! Torn reads are harmless because the data is only visualized.
class ScopeBuffer
{
public:
	static constexpr std::size_t Size = 1024;

	void push(float left, float right)
	{
		const auto i = m_write.load(std::memory_order_relaxed);
		m_left[i] = left;
		m_right[i] = right;
		m_write.store((i + 1) % Size, std::memory_order_relaxed);
	}

	std::size_t writePosition() const { return m_write.load(std::memory_order_relaxed); }
	float left(std::size_t i) const { return m_left[i % Size]; }
	float right(std::size_t i) const { return m_right[i % Size]; }

private:
	std::array<float, Size> m_left{};
	std::array<float, Size> m_right{};
	std::atomic<std::size_t> m_write{0};
};

namespace gui
{

class Knob;

//! Shared palette and painting helpers so all modernized plugins look like one family
namespace modern
{

// Colors follow the default LMMS theme (style.css / LmmsPalette) so these plugins sit
// next to the native ones without standing out
inline QColor windowColor() { return QColor(38, 43, 48); }      // #262b30, theme background
inline QColor sectionColor() { return QColor(30, 34, 38); }     // recessed panel
inline QColor sectionBorder() { return QColor(20, 23, 26); }
inline QColor displayColor() { return QColor(17, 19, 20); }     // #111314, graph screens
inline QColor gridColor() { return QColor(255, 255, 255, 22); }
inline QColor textColor() { return QColor(209, 216, 228); }     // #d1d8e4
inline QColor dimTextColor() { return QColor(138, 147, 156); }
inline QColor buttonColor() { return QColor(63, 71, 80); }      // #3f4750, theme button
inline QColor activeButtonColor() { return QColor(28, 73, 51); } // #1c4933, selected tab

//! The LMMS green, used for values, curves and active states
inline QColor accentColor() { return QColor(29, 226, 118); }    // #1de276
//! Secondary trace color where a display has to tell two signals apart (e.g. right channel)
inline QColor secondaryColor() { return QColor(64, 196, 214); }
//! Highlight for exceptional states such as freeze
inline QColor warningColor() { return QColor(214, 236, 82); }   // #d6ec52, fader warn color

//! Give a plugin dialog the flat dark background shared by all modern plugins
LMMS_EXPORT void applyWindowStyle(QWidget* widget);

//! Paint the dark "screen" used behind curves, waveforms and graphs
LMMS_EXPORT void paintDisplay(QPainter& p, const QRectF& rect, int verticalLines = 8, int horizontalLines = 4);

//! Create a modern knob bound to @p model
LMMS_EXPORT Knob* makeKnob(QWidget* parent, FloatModel* model, const QString& label,
	const QString& hint, const QString& unit);

} // namespace modern


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
