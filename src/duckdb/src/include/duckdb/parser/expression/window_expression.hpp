//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/parser/expression/window_expression.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/parser/parsed_expression.hpp"
#include "duckdb/parser/query_node.hpp"

namespace duckdb {

enum class WindowBoundary : uint8_t {
	INVALID = 0,
	UNBOUNDED_PRECEDING = 1,
	UNBOUNDED_FOLLOWING = 2,
	CURRENT_ROW_RANGE = 3,
	CURRENT_ROW_ROWS = 4,
	EXPR_PRECEDING_ROWS = 5,
	EXPR_FOLLOWING_ROWS = 6,
	EXPR_PRECEDING_RANGE = 7,
	EXPR_FOLLOWING_RANGE = 8
};

//! Represents the window exclusion mode
enum class WindowExcludeMode : uint8_t { NO_OTHER = 0, CURRENT_ROW = 1, GROUP = 2, TIES = 3 };

const char *ToString(WindowBoundary value);

//! The WindowExpression represents a window function in the query. They are a special case of aggregates which is why
//! they inherit from them.
class WindowExpression : public ParsedExpression {
public:
	static constexpr const ExpressionClass TYPE = ExpressionClass::WINDOW;

public:
	WindowExpression(ExpressionType type, string catalog_name, string schema_name, const string &function_name);

	//! Catalog of the aggregate function
	string catalog;
	//! Schema of the aggregate function
	string schema;
	//! Name of the aggregate function
	string function_name;
	//! The child expression of the main window function
	vector<unique_ptr<ParsedExpression>> children;
	//! The set of expressions to partition by
	vector<unique_ptr<ParsedExpression>> partitions;
	//! The set of ordering clauses
	vector<OrderByNode> orders;
	//! Expression representing a filter, only used for aggregates
	unique_ptr<ParsedExpression> filter_expr;
	//! True to ignore NULL values
	bool ignore_nulls;
	//! Whether or not the aggregate function is distinct, only used for aggregates
	bool distinct;
	//! The window boundaries
	WindowBoundary start = WindowBoundary::INVALID;
	WindowBoundary end = WindowBoundary::INVALID;
	//! The EXCLUDE clause
	WindowExcludeMode exclude_clause = WindowExcludeMode::NO_OTHER;

	unique_ptr<ParsedExpression> start_expr;
	unique_ptr<ParsedExpression> end_expr;
	//! Offset and default expressions for WINDOW_LEAD and WINDOW_LAG functions
	unique_ptr<ParsedExpression> offset_expr;
	unique_ptr<ParsedExpression> default_expr;

	//! The set of argument ordering clauses
	//! These are distinct from the frame ordering clauses e.g., the "x" in
	//! FIRST_VALUE(a ORDER BY x) OVER (PARTITION BY p ORDER BY s)
	vector<OrderByNode> arg_orders;

public:
	bool IsWindow() const override {
		return true;
	}

	//! Convert the Expression to a String
	string ToString() const override;

	static bool Equal(const WindowExpression &a, const WindowExpression &b);

	unique_ptr<ParsedExpression> Copy() const override;

	void Serialize(Serializer &serializer) const override;
	static unique_ptr<ParsedExpression> Deserialize(Deserializer &deserializer);

	static ExpressionType WindowToExpressionType(string &fun_name);

public:
	template <class T, class BASE, class ORDER_NODE>
	static string ToString(const T &entry, const string &schema, const string &function_name) {
		// Start with function call
		string result = schema.empty() ? function_name : schema + "." + function_name;
		result += "(";
		if (entry.children.size()) {
			//	Only one DISTINCT is allowed (on the first argument)
			int distincts = entry.distinct ? 0 : 1;
			result += StringUtil::Join(entry.children, entry.children.size(), ", ", [&](const unique_ptr<BASE> &child) {
				return (distincts++ ? "" : "DISTINCT ") + child->ToString();
			});
		}
		// Lead/Lag extra arguments
		if (entry.offset_expr.get()) {
			result += ", ";
			result += entry.offset_expr->ToString();
		}
		if (entry.default_expr.get()) {
			result += ", ";
			result += entry.default_expr->ToString();
		}
		// ORDER BY arguments
		if (!entry.arg_orders.empty()) {
			result += " ORDER BY ";
			result += StringUtil::Join(entry.arg_orders, entry.arg_orders.size(), ", ",
			                           [](const ORDER_NODE &order) { return order.ToString(); });
		}

		// IGNORE NULLS
		if (entry.ignore_nulls) {
			result += " IGNORE NULLS";
		}
		// FILTER
		if (entry.filter_expr) {
			result += ") FILTER (WHERE " + entry.filter_expr->ToString();
		}

		// Over clause
		result += ") OVER (";
		string sep;

		// Partitions
		if (!entry.partitions.empty()) {
			result += "PARTITION BY ";
			result += StringUtil::Join(entry.partitions, entry.partitions.size(), ", ",
			                           [](const unique_ptr<BASE> &partition) { return partition->ToString(); });
			sep = " ";
		}

		// Orders
		if (!entry.orders.empty()) {
			result += sep;
			result += "ORDER BY ";
			result += StringUtil::Join(entry.orders, entry.orders.size(), ", ",
			                           [](const ORDER_NODE &order) { return order.ToString(); });
			sep = " ";
		}

		// Rows/Range
		string units = "ROWS";
		string from;
		switch (entry.start) {
		case WindowBoundary::CURRENT_ROW_RANGE:
		case WindowBoundary::CURRENT_ROW_ROWS:
			from = "CURRENT ROW";
			units = (entry.start == WindowBoundary::CURRENT_ROW_RANGE) ? "RANGE" : "ROWS";
			break;
		case WindowBoundary::UNBOUNDED_PRECEDING:
			if (entry.end != WindowBoundary::CURRENT_ROW_RANGE) {
				from = "UNBOUNDED PRECEDING";
			}
			break;
		case WindowBoundary::EXPR_PRECEDING_ROWS:
		case WindowBoundary::EXPR_PRECEDING_RANGE:
			from = entry.start_expr->ToString() + " PRECEDING";
			units = (entry.start == WindowBoundary::EXPR_PRECEDING_RANGE) ? "RANGE" : "ROWS";
			break;
		case WindowBoundary::EXPR_FOLLOWING_ROWS:
		case WindowBoundary::EXPR_FOLLOWING_RANGE:
			from = entry.start_expr->ToString() + " FOLLOWING";
			units = (entry.start == WindowBoundary::EXPR_FOLLOWING_RANGE) ? "RANGE" : "ROWS";
			break;
		default:
			throw InternalException("Unrecognized FROM in WindowExpression");
		}

		string to;
		switch (entry.end) {
		case WindowBoundary::CURRENT_ROW_RANGE:
			if (entry.start != WindowBoundary::UNBOUNDED_PRECEDING) {
				to = "CURRENT ROW";
				units = "RANGE";
			}
			break;
		case WindowBoundary::CURRENT_ROW_ROWS:
			to = "CURRENT ROW";
			units = "ROWS";
			break;
		case WindowBoundary::UNBOUNDED_PRECEDING:
			to = "UNBOUNDED PRECEDING";
			break;
		case WindowBoundary::UNBOUNDED_FOLLOWING:
			to = "UNBOUNDED FOLLOWING";
			break;
		case WindowBoundary::EXPR_PRECEDING_ROWS:
		case WindowBoundary::EXPR_PRECEDING_RANGE:
			to = entry.end_expr->ToString() + " PRECEDING";
			units = (entry.end == WindowBoundary::EXPR_PRECEDING_RANGE) ? "RANGE" : "ROWS";
			break;
		case WindowBoundary::EXPR_FOLLOWING_ROWS:
		case WindowBoundary::EXPR_FOLLOWING_RANGE:
			to = entry.end_expr->ToString() + " FOLLOWING";
			units = (entry.end == WindowBoundary::EXPR_FOLLOWING_RANGE) ? "RANGE" : "ROWS";
			break;
		default:
			throw InternalException("Unrecognized TO in WindowExpression");
		}
		if (entry.exclude_clause != WindowExcludeMode::NO_OTHER) {
			// if we have an explicit EXCLUDE we always need to fill in from/to
			if (from.empty()) {
				from = "UNBOUNDED PRECEDING";
			}
			if (to.empty()) {
				to = "CURRENT ROW";
				units = "RANGE";
			}
		}

		if (!from.empty() || !to.empty()) {
			result += sep + units;
		}
		if (!from.empty() && !to.empty()) {
			result += " BETWEEN ";
			result += from;
			result += " AND ";
			result += to;
		} else if (!from.empty()) {
			result += " ";
			result += from;
		} else if (!to.empty()) {
			result += " ";
			result += to;
		}

		if (entry.exclude_clause != WindowExcludeMode::NO_OTHER) {
			result += " EXCLUDE ";
		}
		switch (entry.exclude_clause) {
		case WindowExcludeMode::CURRENT_ROW:
			result += "CURRENT ROW";
			break;
		case WindowExcludeMode::GROUP:
			result += "GROUP";
			break;
		case WindowExcludeMode::TIES:
			result += "TIES";
			break;
		default:
			break;
		}

		result += ")";

		return result;
	}

private:
	explicit WindowExpression(ExpressionType type);
};

} // namespace duckdb
