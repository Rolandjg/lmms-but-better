/*
 * FunctionGeneratorDialog.h - dialog for generating periodic shapes into an automation clip
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

#ifndef LMMS_GUI_FUNCTION_GENERATOR_DIALOG_H
#define LMMS_GUI_FUNCTION_GENERATOR_DIALOG_H

#include <QDialog>
#include <vector>

#include "FunctionGenerator.h"
#include "lmms_export.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;

namespace lmms::gui
{

class CurveEditor;
class FunctionPreview;
class GeneratorParam;
class ShapeButton;

//! Function generator with a live preview. The caller applies generator() to the clip on accept.
class LMMS_EXPORT FunctionGeneratorDialog : public QDialog
{
	Q_OBJECT
public:
	//! @param lengthTicks length the shape will be stretched over
	//! @param rangeMin/rangeMax value range of the automated parameter
	FunctionGeneratorDialog(int lengthTicks, float rangeMin, float rangeMax, QWidget* parent = nullptr);

	const FunctionGenerator& generator() const { return m_gen; }
	void setGenerator(const FunctionGenerator& generator);

private slots:
	void paramsChanged();
	void resetParams();
	void reseed();
	void expressionChanged();
	void toggleExpression(bool on);

private:
	void setShape(FunctionGenerator::Shape shape);
	void syncControls();

	FunctionGenerator m_gen;
	int m_lengthTicks;
	float m_rangeMin;
	float m_rangeMax;
	bool m_syncing = false;

	FunctionPreview* m_preview;
	std::vector<ShapeButton*> m_shapeButtons;
	GeneratorParam* m_cycles;
	GeneratorParam* m_phase;
	GeneratorParam* m_symmetry;
	GeneratorParam* m_bend;
	GeneratorParam* m_steps;
	GeneratorParam* m_cells;
	GeneratorParam* m_detail;
	GeneratorParam* m_low;
	GeneratorParam* m_high;
	GeneratorParam* m_freqRange;
	CurveEditor* m_amplitude;
	CurveEditor* m_frequency;
	QToolButton* m_expressionToggle;
	QWidget* m_expressionBox;
	QLineEdit* m_expression;
	QLabel* m_expressionStatus;
	FunctionGenerator::Shape m_shapeBeforeExpression = FunctionGenerator::Shape::Sine;
	QCheckBox* m_invert;
	QLabel* m_info;
	QPushButton* m_reseedButton;
};

} // namespace lmms::gui

#endif // LMMS_GUI_FUNCTION_GENERATOR_DIALOG_H
