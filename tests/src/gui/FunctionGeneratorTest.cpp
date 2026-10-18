/* Copyright (c) 2026 LMMS developers. SPDX-License-Identifier: GPL-2.0-or-later */
#include <QApplication>
#include <QMouseEvent>
#include <QToolButton>
#include <QtTest>

#include "FunctionGenerator.h"
#include "FunctionGeneratorDialog.h"

using namespace lmms;
using Shape = FunctionGenerator::Shape;

class FunctionGeneratorTest : public QObject
{
	Q_OBJECT
private slots:
	void nodesStayInRangeAndOrdered()
	{
		for (int s = 0; s <= static_cast<int>(Shape::Custom); ++s)
		{
			for (int steps : {0, 5})
			{
				for (double bend : {0.0, 0.6})
				{
					FunctionGenerator g;
					g.shape = static_cast<Shape>(s);
					g.steps = steps;
					g.bend = bend;
					g.cycles = 3.5;
					g.phase = 0.3;
					g.low = -10.f;
					g.high = 10.f;
					const auto nodes = g.build(1920);
					QVERIFY2(nodes.size() >= 2, qPrintable(QString("shape %1").arg(s)));
					QVERIFY(nodes.size() <= FunctionGenerator::MaxNodes + 16);
					QCOMPARE(nodes.front().tick, 0.0);
					QCOMPARE(nodes.back().tick, 1920.0);
					for (std::size_t i = 0; i < nodes.size(); ++i)
					{
						QVERIFY(nodes[i].in >= -10.001f && nodes[i].in <= 10.001f);
						QVERIFY(nodes[i].out >= -10.001f && nodes[i].out <= 10.001f);
						if (i > 0) { QVERIFY(nodes[i].tick > nodes[i - 1].tick); }
					}
				}
			}
		}
	}

	void expressionEvaluates()
	{
		FunctionExpression e;
		QVERIFY(e.compile("sin(tau * p) * (1 - t) + 2^-1 * (p > 0.5) - -n").empty());
		QVERIFY(std::abs(e.evaluate(0.25, 0.5) - (0.5 + 0.0)) < 1e-9);
		QVERIFY(std::abs(e.evaluate(1.25, 0.0) - (1.0 + 1.0 + 0.5)) < 1e-9);
		QVERIFY(e.compile("-2^2").empty());
		QCOMPARE(e.evaluate(0, 0), -4.0);
		QVERIFY(e.compile("mix(0, 10, t)").empty());
		QCOMPARE(e.evaluate(0, 0.3), 3.0);
		QVERIFY(e.compile("1/0").empty()); // no NaN or inf escapes
		QCOMPARE(e.evaluate(0, 0), 0.0);

		for (const char* bad : {"", "sin(", "foo(1)", "1 +", "bogus", "min(1)", "(1", "1 2", "sin(1,2)"})
		{
			QVERIFY2(!e.compile(bad).empty(), bad);
			QVERIFY(!e.valid());
		}
	}

	void amplitudeEnvelopeShapesTheSwing()
	{
		FunctionGenerator g;
		g.shape = Shape::Sine;
		g.cycles = 4.0;
		g.amplitude.points = {{0.0, 0.0}, {1.0, 1.0}};
		const auto nodes = g.build(3840);
		float firstSwing = 0.f;
		float lastSwing = 0.f;
		for (const auto& n : nodes)
		{
			const float dev = std::abs(n.out - 0.5f);
			if (n.tick < 960) { firstSwing = std::max(firstSwing, dev); }
			if (n.tick > 2880) { lastSwing = std::max(lastSwing, dev); }
		}
		QVERIFY(firstSwing < 0.15f);
		QVERIFY(lastSwing > 0.4f);
		QCOMPARE(nodes.front().out, 0.5f);

		// a flat full-scale graph changes nothing
		FunctionGenerator plain;
		plain.shape = Shape::Sine;
		plain.cycles = 4.0;
		FunctionGenerator flat = plain;
		flat.amplitude.points = {{0.0, 1.0}, {1.0, 1.0}};
		QCOMPARE(plain.build(3840).size(), flat.build(3840).size());
	}

	void frequencyEnvelopeKeepsTotalCycles()
	{
		FunctionGenerator g;
		g.shape = Shape::SawUp;
		g.cycles = 6.0;
		g.frequency.points = {{0.0, 1.0}, {1.0, 0.0}}; // starts fast, ends slow
		const auto nodes = g.build(6000);
		// every saw reset is a jump; collect where they happen
		std::vector<double> resets;
		for (const auto& n : nodes)
		{
			if (n.in > 0.9f && n.out < 0.1f) { resets.push_back(n.tick); }
		}
		QCOMPARE(resets.size(), std::size_t{5});
		QCOMPARE(nodes.back().tick, 6000.0);
		// cycles get longer towards the end
		QVERIFY(resets[1] - resets[0] < resets[4] - resets[3]);
	}

	void customShapeBuilds()
	{
		FunctionGenerator g;
		g.shape = Shape::Custom;
		g.expression = "sin(tau * p) * (1 - t)";
		g.cycles = 4.0;
		QVERIFY(g.expressionError().empty());
		const auto nodes = g.build(1920);
		QVERIFY(nodes.size() > 32);
		QVERIFY(std::abs(nodes.back().out - 0.5f) < 0.02f);

		g.expression = "sin(";
		QVERIFY(!g.expressionError().empty());
		QVERIFY(!g.build(1920).empty()); // a broken expression still gives a (flat) result
	}

	void graphsSurviveMouseEditing()
	{
		gui::FunctionGeneratorDialog dialog(1920, 0.f, 100.f);
		dialog.show();
		for (const char* className : {"lmms::gui::CurveEditor"})
		{
			const auto editors = dialog.findChildren<QWidget*>();
			int tested = 0;
			for (QWidget* w : editors)
			{
				if (!QString(w->metaObject()->className()).contains("CurveEditor")) { continue; }
				++tested;
				const auto at = [&](QEvent::Type type, QPoint p, Qt::MouseButton b, Qt::MouseButtons down) {
					QMouseEvent e(type, p, w->mapToGlobal(p), b, down, Qt::NoModifier);
					QApplication::sendEvent(w, &e);
				};
				const QPoint a(w->width() / 4, w->height() / 3);
				const QPoint b(w->width() * 3 / 4, w->height() * 2 / 3);
				// add three points, hover over the last, then remove / replace the curve under the hover
				for (QPoint p : {a, b, QPoint(w->width() / 2, 10)})
				{
					at(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
					at(QEvent::MouseMove, p + QPoint(3, 3), Qt::NoButton, Qt::LeftButton);
					at(QEvent::MouseButtonRelease, p + QPoint(3, 3), Qt::LeftButton, Qt::NoButton);
				}
				at(QEvent::MouseMove, QPoint(w->width() / 2, 10), Qt::NoButton, Qt::NoButton);
				w->grab();
				for (auto* button : dialog.findChildren<QToolButton*>())
				{
					if (button->text() == "Off") { button->click(); }
				}
				w->grab();
				at(QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
				at(QEvent::MouseButtonRelease, a, Qt::LeftButton, Qt::NoButton);
				at(QEvent::MouseMove, a, Qt::NoButton, Qt::NoButton);
				at(QEvent::MouseButtonPress, a, Qt::RightButton, Qt::RightButton);
				at(QEvent::MouseButtonRelease, a, Qt::RightButton, Qt::NoButton);
				w->grab();
			}
			QCOMPARE(tested, 2);
			Q_UNUSED(className);
		}
		QVERIFY(!dialog.generator().build(1920).empty());
	}

	void squareHonoursPulseWidth()
	{
		FunctionGenerator g;
		g.shape = Shape::Square;
		g.cycles = 1.0;
		g.symmetry = 0.25;
		QVERIFY(g.valueAt(0.1) > 0.99);
		QVERIFY(g.valueAt(0.5) < 0.01);
		const auto nodes = g.build(400);
		// start, fall at 25%, end
		QCOMPARE(nodes.size(), std::size_t{3});
		QCOMPARE(nodes[1].tick, 100.0);
		QCOMPARE(nodes[1].in, 1.f);
		QCOMPARE(nodes[1].out, 0.f);
	}

	void quantiseMakesPlateaus()
	{
		FunctionGenerator g;
		g.shape = Shape::SawUp;
		g.cycles = 1.0;
		g.steps = 4;
		const auto nodes = g.build(960);
		// every plateau is bounded by nodes of equal in/out level on one side
		for (std::size_t i = 1; i + 1 < nodes.size(); ++i)
		{
			QVERIFY(nodes[i].in == nodes[i - 1].out);
		}
	}

	void dialogRenders()
	{
		const QString theme = qEnvironmentVariable("FUNCGEN_THEME");
		if (!theme.isEmpty())
		{
			QFile css(theme);
			if (css.open(QIODevice::ReadOnly)) { qApp->setStyleSheet(QString::fromUtf8(css.readAll())); }
		}
		gui::FunctionGeneratorDialog dialog(1920, 0.f, 100.f);
		dialog.show();
		const QString dir = qEnvironmentVariable("FUNCGEN_SNAPSHOTS");
		if (!dir.isEmpty()) { QVERIFY(dialog.grab().save(dir + "/function_generator.png")); }
		QVERIFY(!dialog.generator().build(1920).empty());

		// graphs and the expression box
		FunctionGenerator g = dialog.generator();
		g.shape = Shape::Custom;
		g.expression = "sin(tau * p) * (1 - t)";
		g.cycles = 8.0;
		g.amplitude.points = {{0.0, 0.2}, {0.4, 1.0}, {1.0, 0.6}};
		g.frequency.points = {{0.0, 1.0}, {1.0, 0.0}};
		dialog.setGenerator(g);
		QVERIFY(dialog.generator().frequencyActive());
		if (!dir.isEmpty()) { QVERIFY(dialog.grab().save(dir + "/function_generator_graphs.png")); }
	}
};

int main(int argc, char** argv)
{
	QApplication app{argc, argv};
	FunctionGeneratorTest test;
	return QTest::qExec(&test, argc, argv);
}
#include "FunctionGeneratorTest.moc"
