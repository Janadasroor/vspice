#ifndef SIM_EXPRESSION_H
#define SIM_EXPRESSION_H

#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <stack>
#include <stdexcept>
#include <functional>
#include <iostream>

namespace Sim {

enum class ExprTokenType {
    Number, Variable, Function, Operator, OpenParen, CloseParen, Comma, End
};

struct ExprToken {
    ExprTokenType type;
    std::string value;
};

class Expression {
public:
    Expression() {}
    explicit Expression(const std::string& expr) : m_source(expr) {
        parse();
    }

    double evaluate(const std::map<std::string, double>& variables) const {
        if (m_rpn.empty()) return 0.0;

        std::stack<double> stack;
        for (const auto& token : m_rpn) {
            if (token.type == ExprTokenType::Number) {
                stack.push(std::stod(token.value));
            } else if (token.type == ExprTokenType::Variable) {
                if (variables.count(token.value)) {
                    stack.push(variables.at(token.value));
                } else {
                    stack.push(0.0); // Default to 0
                }
            } else if (token.type == ExprTokenType::Operator) {
                if (stack.size() < 2) throw std::runtime_error("Stack underflow for operator");
                double b = stack.top(); stack.pop();
                double a = stack.top(); stack.pop();
                
                if (token.value == "+") stack.push(a + b);
                else if (token.value == "-") stack.push(a - b);
                else if (token.value == "*") stack.push(a * b);
                else if (token.value == "/") stack.push(b != 0 ? a / b : 0);
                else if (token.value == "^") stack.push(std::pow(a, b));
            } else if (token.type == ExprTokenType::Function) {
                if (stack.empty()) throw std::runtime_error("Stack underflow for function");
                double a = stack.top(); stack.pop();
                
                if (token.value == "sin") stack.push(std::sin(a));
                else if (token.value == "cos") stack.push(std::cos(a));
                else if (token.value == "exp") stack.push(std::exp(a));
                else if (token.value == "log") stack.push(std::log(a));
                else if (token.value == "sqrt") stack.push(std::sqrt(a));
                else if (token.value == "abs") stack.push(std::abs(a));
            }
        }
        return stack.empty() ? 0.0 : stack.top();
    }

    // Basic numerical derivative dF/dx
    double derivative(const std::string& var, std::map<std::string, double> variables, double delta = 1e-6) const {
        double x0 = variables[var];
        variables[var] = x0 + delta;
        double f1 = evaluate(variables);
        variables[var] = x0 - delta;
        double f2 = evaluate(variables);
        variables[var] = x0;
        return (f1 - f2) / (2.0 * delta);
    }

    const std::string& source() const { return m_source; }
    bool isValid() const { return !m_rpn.empty(); }

    std::vector<std::string> getVariables() const {
        std::vector<std::string> vars;
        for (const auto& token : m_rpn) {
            if (token.type == ExprTokenType::Variable) {
                bool found = false;
                for (const auto& v : vars) if (v == token.value) { found = true; break; }
                if (!found) vars.push_back(token.value);
            }
        }
        return vars;
    }

private:
    void parse() {
        std::vector<ExprToken> tokens = tokenize(m_source);
        m_rpn = shuntingYard(tokens);
    }

    std::vector<ExprToken> tokenize(const std::string& s) {
        std::vector<ExprToken> tokens;
        size_t i = 0;
        while (i < s.length()) {
            char c = s[i];
            if (std::isspace(c)) { i++; continue; }

            if (std::isdigit(c) || c == '.') {
                std::string num;
                while (i < s.length() && (std::isdigit(s[i]) || s[i] == '.')) num += s[i++];
                tokens.push_back({ ExprTokenType::Number, num });
            } else if (std::isalpha(c)) {
                std::string name;
                while (i < s.length() && (std::isalnum(s[i]) || s[i] == '_' || s[i] == '(' || s[i] == ')')) {
                    // Special handling for V(node) and I(comp)
                    if (s[i] == '(') {
                        // Look ahead to see if it's a function or variable
                        // For now we treat V(...) as a single variable name
                        name += s[i++];
                        while (i < s.length() && s[i] != ')') name += s[i++];
                        if (i < s.length()) name += s[i++];
                        break; 
                    }
                    name += s[i++];
                }
                
                // Check if it's a known function
                if (name == "sin" || name == "cos" || name == "exp" || name == "log" || name == "sqrt" || name == "abs") {
                    tokens.push_back({ ExprTokenType::Function, name });
                } else {
                    tokens.push_back({ ExprTokenType::Variable, name });
                }
            } else if (c == '(') {
                tokens.push_back({ ExprTokenType::OpenParen, "(" });
                i++;
            } else if (c == ')') {
                tokens.push_back({ ExprTokenType::CloseParen, ")" });
                i++;
            } else if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^') {
                tokens.push_back({ ExprTokenType::Operator, std::string(1, c) });
                i++;
            } else {
                i++; // Skip unknown
            }
        }
        return tokens;
    }

    std::vector<ExprToken> shuntingYard(const std::vector<ExprToken>& tokens) {
        std::vector<ExprToken> output;
        std::stack<ExprToken> operators;

        auto precedence = [](const std::string& op) {
            if (op == "+" || op == "-") return 1;
            if (op == "*" || op == "/") return 2;
            if (op == "^") return 3;
            return 0;
        };

        for (const auto& token : tokens) {
            if (token.type == ExprTokenType::Number || token.type == ExprTokenType::Variable) {
                output.push_back(token);
            } else if (token.type == ExprTokenType::Function) {
                operators.push(token);
            } else if (token.type == ExprTokenType::Operator) {
                while (!operators.empty() && operators.top().type == ExprTokenType::Operator &&
                       precedence(operators.top().value) >= precedence(token.value)) {
                    output.push_back(operators.top());
                    operators.pop();
                }
                operators.push(token);
            } else if (token.type == ExprTokenType::OpenParen) {
                operators.push(token);
            } else if (token.type == ExprTokenType::CloseParen) {
                while (!operators.empty() && operators.top().type != ExprTokenType::OpenParen) {
                    output.push_back(operators.top());
                    operators.pop();
                }
                if (!operators.empty()) operators.pop(); // Remove '('
                if (!operators.empty() && operators.top().type == ExprTokenType::Function) {
                    output.push_back(operators.top());
                    operators.pop();
                }
            }
        }
        while (!operators.empty()) {
            output.push_back(operators.top());
            operators.pop();
        }
        return output;
    }

    std::string m_source;
    std::vector<ExprToken> m_rpn;
};

} // namespace Sim

#endif // SIM_EXPRESSION_H
