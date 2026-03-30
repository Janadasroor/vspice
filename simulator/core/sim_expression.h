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
    int varIdx = -1; 
};

class Expression {
public:
    Expression() {}
    explicit Expression(const std::string& expr) : m_source(expr) {
        parse();
    }

    double evaluate(const std::map<std::string, double>& variables) const {
        if (m_rpn.empty()) return 0.0;
        std::vector<double> vals;
        auto vars = getVariables();
        for (const auto& v : vars) vals.push_back(variables.count(v) ? variables.at(v) : 0.0);
        return evaluate(vals);
    }

    double evaluate(const std::vector<double>& values) const {
        return evaluate(values.data(), (int)values.size());
    }

    double evaluate(const double* values, int count) const {
        if (m_rpn.empty()) return 0.0;
        double stack[64]; 
        int top = -1;

        for (const auto& token : m_rpn) {
            if (token.type == ExprTokenType::Number) {
                stack[++top] = std::stod(token.value);
            } else if (token.type == ExprTokenType::Variable) {
                stack[++top] = (token.varIdx >= 0 && token.varIdx < count) ? values[token.varIdx] : 0.0;
            } else if (token.type == ExprTokenType::Operator) {
                if (top < 1) { top = -1; break; }
                double b = stack[top--];
                double a = stack[top];
                if (token.value == "+") stack[top] = a + b;
                else if (token.value == "-") stack[top] = a - b;
                else if (token.value == "*") stack[top] = a * b;
                else if (token.value == "/") stack[top] = (b != 0 ? a / b : 0);
                else if (token.value == "^") stack[top] = std::pow(a, b);
            } else if (token.type == ExprTokenType::Function) {
                if (top < 0) continue;
                double a = stack[top];
                if (token.value == "sin") stack[top] = std::sin(a);
                else if (token.value == "cos") stack[top] = std::cos(a);
                else if (token.value == "exp") stack[top] = std::exp(a);
                else if (token.value == "log") stack[top] = std::log(a);
                else if (token.value == "sqrt") stack[top] = std::sqrt(a);
                else if (token.value == "abs") stack[top] = std::abs(a);
            }
        }
        return top < 0 ? 0.0 : stack[top];
    }

    double derivative(int varIdx, double* values, int count, double delta = 1e-6) const {
        if (varIdx < 0 || varIdx >= count) return 0.0;
        double x0 = values[varIdx];
        values[varIdx] = x0 + delta;
        double f1 = evaluate(values, count);
        values[varIdx] = x0 - delta;
        double f2 = evaluate(values, count);
        values[varIdx] = x0;
        return (f1 - f2) / (2.0 * delta);
    }

    double derivative(int varIdx, std::vector<double>& values, double delta = 1e-6) const {
        if (varIdx < 0 || varIdx >= (int)values.size()) return 0.0;
        double x0 = values[varIdx];
        values[varIdx] = x0 + delta;
        double f1 = evaluate(values);
        values[varIdx] = x0 - delta;
        double f2 = evaluate(values);
        values[varIdx] = x0;
        return (f1 - f2) / (2.0 * delta);
    }

    double derivative(const std::string& var, std::map<std::string, double> variables, double delta = 1e-6) const {
        auto vars = getVariables();
        int idx = -1;
        for (int i = 0; i < (int)vars.size(); ++i) if (vars[i] == var) { idx = i; break; }
        if (idx == -1) return 0.0;
        
        std::vector<double> vals;
        for (const auto& v : vars) vals.push_back(variables.count(v) ? variables.at(v) : 0.0);
        return derivative(idx, vals, delta);
    }

    const std::string& source() const { return m_source; }
    bool isValid() const { return !m_rpn.empty(); }

    std::vector<std::string> getVariables() const {
        return m_variables;
    }

private:
    static bool isKnownFunctionName(const std::string& name) {
        return name == "sin" || name == "cos" || name == "exp" || name == "log" ||
               name == "sqrt" || name == "abs";
    }

    void parse() {
        std::vector<ExprToken> tokens = tokenize(m_source);
        m_rpn = shuntingYard(tokens);
        
        // Extract variables and assign indices to tokens
        m_variables.clear();
        for (auto& token : m_rpn) {
            if (token.type == ExprTokenType::Variable) {
                int foundIdx = -1;
                for (int i = 0; i < (int)m_variables.size(); ++i) {
                    if (m_variables[i] == token.value) { foundIdx = i; break; }
                }
                if (foundIdx == -1) {
                    foundIdx = (int)m_variables.size();
                    m_variables.push_back(token.value);
                }
                token.varIdx = foundIdx;
            }
        }
    }
    // ... rest of private methods (tokenize, shuntingYard) ...

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
                while (i < s.length() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) {
                    name += s[i++];
                }

                if (i < s.length() && s[i] == '(' && isKnownFunctionName(name)) {
                    tokens.push_back({ ExprTokenType::Function, name });
                } else {
                    if (i < s.length() && s[i] == '(') {
                        int depth = 0;
                        do {
                            if (s[i] == '(') ++depth;
                            else if (s[i] == ')') --depth;
                            name += s[i++];
                        } while (i < s.length() && depth > 0);
                    }
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
    std::vector<std::string> m_variables;
};

} // namespace Sim

#endif // SIM_EXPRESSION_H
