/*
 * ControlAccessibility.cpp - keyboard focus and assistive-technology support for custom-drawn controls
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

#include "ControlAccessibility.h"

#include <QAccessible>
#include <QAccessibleValueInterface>
#include <QAccessibleWidget>
#include <QFocusEvent>
#include <QPainter>
#include <QWidget>

#include "ComboBox.h"
#include "ComboBoxModel.h"
#include "Fader.h"
#include "FloatModelEditorBase.h"
#include "Knob.h"
#include "LcdFloatSpinBox.h"
#include "LcdSpinBox.h"
#include "ModernWidgets.h"

namespace lmms::gui
{

namespace
{

class FocusRing : public QWidget
{
public:
	explicit FocusRing(QWidget* parent) : QWidget(parent)
	{
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setFocusPolicy(Qt::NoFocus);
		setObjectName("lmmsFocusRing");
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		p.setPen(QPen(modern::accentColor(), 1));
		p.setBrush(Qt::NoBrush);
		p.drawRect(rect().adjusted(0, 0, -1, -1));
	}
};


class FocusRingFilter : public QObject
{
public:
	using QObject::QObject;

	bool eventFilter(QObject* watched, QEvent* event) override
	{
		auto control = static_cast<QWidget*>(watched);
		switch (event->type())
		{
			case QEvent::FocusIn:
			{
				const auto reason = static_cast<QFocusEvent*>(event)->reason();
				if (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason
					|| reason == Qt::ShortcutFocusReason)
				{
				auto ring = control->findChild<QWidget*>("lmmsFocusRing", Qt::FindDirectChildrenOnly);
					if (!ring) { ring = new FocusRing(control); }
					ring->setGeometry(control->rect());
					ring->raise();
					ring->show();
				}
				break;
			}
			case QEvent::FocusOut:
				if (auto ring = control->findChild<QWidget*>("lmmsFocusRing", Qt::FindDirectChildrenOnly)) { ring->hide(); }
				break;
			case QEvent::Resize:
				if (auto ring = control->findChild<QWidget*>("lmmsFocusRing", Qt::FindDirectChildrenOnly))
				{
					ring->setGeometry(control->rect());
				}
				break;
			default:
				break;
		}
		return false;
	}
};


//! Accessibility interface for a widget showing an AutomatableModel
class ModelControlAccessible : public QAccessibleWidget, public QAccessibleValueInterface
{
public:
	ModelControlAccessible(QWidget* widget, QAccessible::Role role) : QAccessibleWidget(widget, role) {}

	QString text(QAccessible::Text t) const override
	{
		switch (t)
		{
			case QAccessible::Name: return name();
			case QAccessible::Value: return valueText();
			case QAccessible::Description:
				if (auto editor = qobject_cast<FloatModelEditorBase*>(widget()); editor && !editor->toolTip().isEmpty())
				{
					return editor->toolTip();
				}
				return QAccessibleWidget::text(t);
			default: return QAccessibleWidget::text(t);
		}
	}

	void* interface_cast(QAccessible::InterfaceType type) override
	{
		if (type == QAccessible::ValueInterface && role() != QAccessible::ComboBox && model())
		{
			return static_cast<QAccessibleValueInterface*>(this);
		}
		return QAccessibleWidget::interface_cast(type);
	}

	QVariant currentValue() const override { return model() ? model()->value<float>() : QVariant{}; }
	void setCurrentValue(const QVariant& value) override { if (model()) { model()->setValue(value.toFloat()); } }
	QVariant maximumValue() const override { return model() ? model()->maxValue<float>() : QVariant{}; }
	QVariant minimumValue() const override { return model() ? model()->minValue<float>() : QVariant{}; }
	QVariant minimumStepSize() const override { return model() ? model()->step<float>() : QVariant{}; }

	QStringList actionNames() const override
	{
		return QAccessibleWidget::actionNames() << increaseAction() << decreaseAction();
	}

	void doAction(const QString& action) override
	{
		const int direction = action == increaseAction() ? 1 : action == decreaseAction() ? -1 : 0;
		if (direction == 0 || !model()) { QAccessibleWidget::doAction(action); return; }
		if (auto editor = qobject_cast<FloatModelEditorBase*>(widget())) { editor->stepValue(direction); }
		else { model()->incValue(direction); }
	}

private:
	AutomatableModelView* view() const { return dynamic_cast<AutomatableModelView*>(widget()); }
	AutomatableModel* model() const { return view() ? view()->modelUntyped() : nullptr; }

	QString name() const
	{
		if (auto editor = qobject_cast<FloatModelEditorBase*>(widget())) { return editor->accessibleNameText(); }
		auto name = widget()->accessibleName();
		if (name.isEmpty() && view()) { name = view()->description(); }
		if (name.isEmpty() && model()) { name = model()->displayName(); }
		if (name.isEmpty()) { name = widget()->windowTitle(); }
		if (name.endsWith(':')) { name.chop(1); }
		return name.trimmed();
	}

	QString valueText() const
	{
		if (!model()) { return {}; }
		if (auto editor = qobject_cast<FloatModelEditorBase*>(widget())) { return editor->accessibleValueText(); }
		if (auto comboModel = dynamic_cast<ComboBoxModel*>(model())) { return comboModel->currentText(); }
		// the fader's tooltip holds its value in dB
		if (qobject_cast<Fader*>(widget())) { return widget()->toolTip(); }
		const auto factor = view()->getConversionFactor();
		return QString::number(model()->value<float>() * factor) + view()->unit();
	}
};


QAccessibleInterface* controlFactory(const QString&, QObject* object)
{
	auto widget = qobject_cast<QWidget*>(object);
	if (!widget) { return nullptr; }
	if (qobject_cast<Knob*>(widget)) { return new ModelControlAccessible(widget, QAccessible::Dial); }
	if (qobject_cast<FloatModelEditorBase*>(widget) || qobject_cast<Fader*>(widget))
	{
		return new ModelControlAccessible(widget, QAccessible::Slider);
	}
	if (qobject_cast<ComboBox*>(widget)) { return new ModelControlAccessible(widget, QAccessible::ComboBox); }
	if (qobject_cast<LcdSpinBox*>(widget) || qobject_cast<LcdFloatSpinBox*>(widget))
	{
		return new ModelControlAccessible(widget, QAccessible::SpinBox);
	}
	return nullptr;
}

} // namespace


void attachFocusRing(QWidget* control)
{
	static auto filter = new FocusRingFilter;
	control->installEventFilter(filter);
}


void announceValueChange(QWidget* control, float value)
{
	if (!QAccessible::isActive()) { return; }
	QAccessibleValueChangeEvent event(control, value);
	QAccessible::updateAccessibility(&event);
}


void installControlAccessibility()
{
	static bool installed = false;
	if (installed) { return; }
	installed = true;
	QAccessible::installFactory(controlFactory);
}

} // namespace lmms::gui
