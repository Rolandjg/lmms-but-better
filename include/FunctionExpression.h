/*
 * FunctionExpression.h - tiny math expression evaluator for the function generator
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
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef LMMS_FUNCTION_EXPRESSION_H
#define LMMS_FUNCTION_EXPRESSION_H

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

namespace lmms
{

//! A compiled expression over the variables `p` (position in cycles), `t` (0..1 over the whole
//! stretch) and `n` (cycle number). Operators: + - * / % ^ and the comparisons < > <= >= == !=
//! (which give 1 or 0). Functions: sin cos tan asin acos atan atan2 sinh cosh tanh abs sqrt exp
//! ln log2 floor ceil round fract sign min max pow mod clamp mix step smoothstep noise.
//! Constants: pi, tau, e.
class FunctionExpression
{
public:
	//! Returns an empty string on success, otherwise a message describing the problem
	std::string compile(std::string_view source)
	{
		m_nodes.clear();
		m_root = -1;
		if (source.size() > MaxLength) { return "expression is too long"; }

		Parser parser{source, m_nodes};
		const std::string error = parser.parse();
		if (!error.empty())
		{
			m_nodes.clear();
			return error;
		}
		m_root = parser.root();
		return {};
	}

	bool valid() const { return m_root >= 0; }

	double evaluate(double p, double t) const
	{
		if (!valid()) { return 0.0; }
		const Vars vars{p, t, std::floor(p)};
		const double v = eval(m_root, vars);
		return std::isfinite(v) ? v : 0.0;
	}

private:
	static constexpr std::size_t MaxLength = 512;
	static constexpr std::size_t MaxNodes = 256;

	enum class Op
	{
		Const, VarP, VarT, VarN,
		Add, Sub, Mul, Div, Mod, Pow, Neg,
		Less, Greater, LessEq, GreaterEq, Equal, NotEqual,
		Call,
	};

	enum class Fn
	{
		Sin, Cos, Tan, Asin, Acos, Atan, Atan2, Sinh, Cosh, Tanh, Abs, Sqrt, Exp, Ln, Log2,
		Floor, Ceil, Round, Fract, Sign, Min, Max, Pow, Mod, Clamp, Mix, Step, Smoothstep, Noise,
	};

	struct FnInfo
	{
		const char* name;
		Fn fn;
		int args;
	};

	static constexpr FnInfo Functions[] = {
		{"sin", Fn::Sin, 1}, {"cos", Fn::Cos, 1}, {"tan", Fn::Tan, 1}, {"asin", Fn::Asin, 1},
		{"acos", Fn::Acos, 1}, {"atan", Fn::Atan, 1}, {"atan2", Fn::Atan2, 2}, {"sinh", Fn::Sinh, 1},
		{"cosh", Fn::Cosh, 1}, {"tanh", Fn::Tanh, 1}, {"abs", Fn::Abs, 1}, {"sqrt", Fn::Sqrt, 1},
		{"exp", Fn::Exp, 1}, {"ln", Fn::Ln, 1}, {"log2", Fn::Log2, 1}, {"floor", Fn::Floor, 1},
		{"ceil", Fn::Ceil, 1}, {"round", Fn::Round, 1}, {"fract", Fn::Fract, 1}, {"sign", Fn::Sign, 1},
		{"min", Fn::Min, 2}, {"max", Fn::Max, 2}, {"pow", Fn::Pow, 2}, {"mod", Fn::Mod, 2},
		{"clamp", Fn::Clamp, 3}, {"mix", Fn::Mix, 3}, {"step", Fn::Step, 2},
		{"smoothstep", Fn::Smoothstep, 3}, {"noise", Fn::Noise, 1},
	};

	struct Node
	{
		Op op = Op::Const;
		Fn fn = Fn::Sin;
		double value = 0.0;
		int a = -1;
		int b = -1;
		int c = -1;
	};

	struct Vars
	{
		double p, t, n;
	};

	static double hashNoise(double x)
	{
		const double fl = std::floor(x);
		const auto hash = [](double v) {
			auto z = static_cast<std::uint64_t>(static_cast<std::int64_t>(v)) * 0x9E3779B97F4A7C15ull;
			z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
			z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
			z ^= z >> 31;
			return static_cast<double>(z >> 11) / static_cast<double>(1ull << 53) * 2.0 - 1.0;
		};
		double u = x - fl;
		u = u * u * (3.0 - 2.0 * u);
		return hash(fl) + (hash(fl + 1.0) - hash(fl)) * u;
	}

	static double callFn(Fn fn, double a, double b, double c)
	{
		switch (fn)
		{
			case Fn::Sin: return std::sin(a);
			case Fn::Cos: return std::cos(a);
			case Fn::Tan: return std::tan(a);
			case Fn::Asin: return std::asin(std::clamp(a, -1.0, 1.0));
			case Fn::Acos: return std::acos(std::clamp(a, -1.0, 1.0));
			case Fn::Atan: return std::atan(a);
			case Fn::Atan2: return std::atan2(a, b);
			case Fn::Sinh: return std::sinh(a);
			case Fn::Cosh: return std::cosh(a);
			case Fn::Tanh: return std::tanh(a);
			case Fn::Abs: return std::abs(a);
			case Fn::Sqrt: return std::sqrt(std::max(a, 0.0));
			case Fn::Exp: return std::exp(a);
			case Fn::Ln: return a > 0.0 ? std::log(a) : 0.0;
			case Fn::Log2: return a > 0.0 ? std::log2(a) : 0.0;
			case Fn::Floor: return std::floor(a);
			case Fn::Ceil: return std::ceil(a);
			case Fn::Round: return std::round(a);
			case Fn::Fract: return a - std::floor(a);
			case Fn::Sign: return a > 0.0 ? 1.0 : a < 0.0 ? -1.0 : 0.0;
			case Fn::Min: return std::min(a, b);
			case Fn::Max: return std::max(a, b);
			case Fn::Pow: return std::pow(a, b);
			case Fn::Mod: return b != 0.0 ? a - b * std::floor(a / b) : 0.0;
			case Fn::Clamp: return std::clamp(a, std::min(b, c), std::max(b, c));
			case Fn::Mix: return a + (b - a) * c;
			case Fn::Step: return b >= a ? 1.0 : 0.0;
			case Fn::Smoothstep:
			{
				if (a == b) { return c >= a ? 1.0 : 0.0; }
				const double x = std::clamp((c - a) / (b - a), 0.0, 1.0);
				return x * x * (3.0 - 2.0 * x);
			}
			case Fn::Noise: return hashNoise(a);
		}
		return 0.0;
	}

	double eval(int index, const Vars& v) const
	{
		const Node& n = m_nodes[index];
		switch (n.op)
		{
			case Op::Const: return n.value;
			case Op::VarP: return v.p;
			case Op::VarT: return v.t;
			case Op::VarN: return v.n;
			case Op::Neg: return -eval(n.a, v);
			case Op::Call:
				return callFn(n.fn, eval(n.a, v), n.b >= 0 ? eval(n.b, v) : 0.0, n.c >= 0 ? eval(n.c, v) : 0.0);
			default: break;
		}

		const double a = eval(n.a, v);
		const double b = eval(n.b, v);
		switch (n.op)
		{
			case Op::Add: return a + b;
			case Op::Sub: return a - b;
			case Op::Mul: return a * b;
			case Op::Div: return b != 0.0 ? a / b : 0.0;
			case Op::Mod: return b != 0.0 ? a - b * std::floor(a / b) : 0.0;
			case Op::Pow: return std::pow(a, b);
			case Op::Less: return a < b ? 1.0 : 0.0;
			case Op::Greater: return a > b ? 1.0 : 0.0;
			case Op::LessEq: return a <= b ? 1.0 : 0.0;
			case Op::GreaterEq: return a >= b ? 1.0 : 0.0;
			case Op::Equal: return a == b ? 1.0 : 0.0;
			case Op::NotEqual: return a != b ? 1.0 : 0.0;
			default: return 0.0;
		}
	}

	//! Recursive descent: comparison < additive < multiplicative < unary minus < power
	class Parser
	{
	public:
		Parser(std::string_view s, std::vector<Node>& nodes)
			: m_s(s)
			, m_nodes(nodes)
		{
		}

		std::string parse()
		{
			try
			{
				skip();
				if (m_pos >= m_s.size()) { return "expression is empty"; }
				m_root = comparison();
				skip();
				if (m_pos < m_s.size()) { fail(std::string("unexpected '") + m_s[m_pos] + "'"); }
			}
			catch (const std::string& error)
			{
				return error;
			}
			return {};
		}

		int root() const { return m_root; }

	private:
		[[noreturn]] void fail(const std::string& what) const { throw what; }

		void skip()
		{
			while (m_pos < m_s.size() && (m_s[m_pos] == ' ' || m_s[m_pos] == '\t')) { ++m_pos; }
		}

		bool eat(char c)
		{
			skip();
			if (m_pos < m_s.size() && m_s[m_pos] == c)
			{
				++m_pos;
				return true;
			}
			return false;
		}

		bool eatPair(char a, char b)
		{
			skip();
			if (m_pos + 1 < m_s.size() && m_s[m_pos] == a && m_s[m_pos + 1] == b)
			{
				m_pos += 2;
				return true;
			}
			return false;
		}

		int add(Node n)
		{
			if (m_nodes.size() >= MaxNodes) { fail("expression is too complicated"); }
			m_nodes.push_back(n);
			return static_cast<int>(m_nodes.size()) - 1;
		}

		int binary(Op op, int a, int b)
		{
			Node n;
			n.op = op;
			n.a = a;
			n.b = b;
			return add(n);
		}

		int comparison()
		{
			int left = additive();
			for (;;)
			{
				if (eatPair('<', '=')) { left = binary(Op::LessEq, left, additive()); }
				else if (eatPair('>', '=')) { left = binary(Op::GreaterEq, left, additive()); }
				else if (eatPair('=', '=')) { left = binary(Op::Equal, left, additive()); }
				else if (eatPair('!', '=')) { left = binary(Op::NotEqual, left, additive()); }
				else if (eat('<')) { left = binary(Op::Less, left, additive()); }
				else if (eat('>')) { left = binary(Op::Greater, left, additive()); }
				else { return left; }
			}
		}

		int additive()
		{
			int left = multiplicative();
			for (;;)
			{
				if (eat('+')) { left = binary(Op::Add, left, multiplicative()); }
				else if (eat('-')) { left = binary(Op::Sub, left, multiplicative()); }
				else { return left; }
			}
		}

		int multiplicative()
		{
			int left = unary();
			for (;;)
			{
				if (eat('*')) { left = binary(Op::Mul, left, unary()); }
				else if (eat('/')) { left = binary(Op::Div, left, unary()); }
				else if (eat('%')) { left = binary(Op::Mod, left, unary()); }
				else { return left; }
			}
		}

		int unary()
		{
			if (++m_depth > 64) { fail("expression is nested too deeply"); }
			int result;
			if (eat('-'))
			{
				Node n;
				n.op = Op::Neg;
				n.a = unary();
				result = add(n);
			}
			else if (eat('+')) { result = unary(); }
			else { result = power(); }
			--m_depth;
			return result;
		}

		int power()
		{
			const int base = primary();
			if (eat('^')) { return binary(Op::Pow, base, unary()); } // right associative, binds tighter than unary minus on the left
			return base;
		}

		int primary()
		{
			skip();
			if (m_pos >= m_s.size()) { fail("expression ends unexpectedly"); }
			const char c = m_s[m_pos];

			if (c == '(')
			{
				++m_pos;
				const int inner = comparison();
				if (!eat(')')) { fail("missing ')'"); }
				return inner;
			}

			if ((c >= '0' && c <= '9') || c == '.')
			{
				std::size_t end = m_pos;
				while (end < m_s.size() && ((m_s[end] >= '0' && m_s[end] <= '9') || m_s[end] == '.')) { ++end; }
				if (end < m_s.size() && (m_s[end] == 'e' || m_s[end] == 'E') && end + 1 < m_s.size()
					&& ((m_s[end + 1] >= '0' && m_s[end + 1] <= '9') || m_s[end + 1] == '-' || m_s[end + 1] == '+'))
				{
					end += 2;
					while (end < m_s.size() && m_s[end] >= '0' && m_s[end] <= '9') { ++end; }
				}
				const std::string text(m_s.substr(m_pos, end - m_pos));
				char* parsedEnd = nullptr;
				const double value = std::strtod(text.c_str(), &parsedEnd);
				if (parsedEnd != text.c_str() + text.size()) { fail("bad number '" + text + "'"); }
				m_pos = end;
				Node n;
				n.value = value;
				return add(n);
			}

			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')
			{
				std::size_t end = m_pos;
				while (end < m_s.size() && ((m_s[end] >= 'a' && m_s[end] <= 'z') || (m_s[end] >= 'A' && m_s[end] <= 'Z')
					|| (m_s[end] >= '0' && m_s[end] <= '9') || m_s[end] == '_')) { ++end; }
				std::string name(m_s.substr(m_pos, end - m_pos));
				std::transform(name.begin(), name.end(), name.begin(), [](char ch) { return static_cast<char>(std::tolower(ch)); });
				m_pos = end;

				if (eat('(')) { return call(name); }

				Node n;
				if (name == "p") { n.op = Op::VarP; }
				else if (name == "t") { n.op = Op::VarT; }
				else if (name == "n") { n.op = Op::VarN; }
				else if (name == "pi") { n.value = std::numbers::pi; }
				else if (name == "tau") { n.value = 2.0 * std::numbers::pi; }
				else if (name == "e") { n.value = std::numbers::e; }
				else { fail("unknown name '" + name + "' (use p, t, n, pi)"); }
				return add(n);
			}

			fail(std::string("unexpected '") + c + "'");
		}

		int call(const std::string& name)
		{
			const FnInfo* info = nullptr;
			for (const auto& f : Functions)
			{
				if (name == f.name) { info = &f; }
			}
			if (!info) { fail("unknown function '" + name + "'"); }

			int args[3] = {-1, -1, -1};
			int count = 0;
			if (!eat(')'))
			{
				do
				{
					if (count >= 3) { fail("too many arguments for " + name); }
					args[count++] = comparison();
				} while (eat(','));
				if (!eat(')')) { fail("missing ')' after " + name); }
			}
			if (count != info->args)
			{
				fail(name + " takes " + std::to_string(info->args) + " argument" + (info->args == 1 ? "" : "s"));
			}

			Node n;
			n.op = Op::Call;
			n.fn = info->fn;
			n.a = args[0];
			n.b = args[1];
			n.c = args[2];
			return add(n);
		}

		std::string_view m_s;
		std::vector<Node>& m_nodes;
		std::size_t m_pos = 0;
		int m_root = -1;
		int m_depth = 0;
	};

	std::vector<Node> m_nodes;
	int m_root = -1;
};

} // namespace lmms

#endif // LMMS_FUNCTION_EXPRESSION_H
