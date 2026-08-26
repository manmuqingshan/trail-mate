#include "ui/screens/calculator/calculator_engine.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace calculator::ui::model
{
namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kDomainEpsilon = 1.0e-10;

bool isFinite(double value)
{
    return std::isfinite(value);
}

void formatValue(double value, char* out, size_t out_len)
{
    if (std::fabs(value) < kDomainEpsilon)
    {
        value = 0.0;
    }
    std::snprintf(out, out_len, "%.10g", value);
}

const char* operationSymbol(Operation operation)
{
    switch (operation)
    {
    case Operation::Add:
        return "+";
    case Operation::Subtract:
        return "-";
    case Operation::Multiply:
        return "*";
    case Operation::Divide:
        return "/";
    case Operation::Power:
        return "^";
    }
    return "";
}

const char* functionToken(Function function)
{
    switch (function)
    {
    case Function::Sin:
        return "sin";
    case Function::Cos:
        return "cos";
    case Function::Tan:
        return "tan";
    case Function::Asin:
        return "asin";
    case Function::Acos:
        return "acos";
    case Function::Atan:
        return "atan";
    case Function::Square:
        return "sqr";
    case Function::SquareRoot:
        return "sqrt";
    case Function::Reciprocal:
        return "inv";
    case Function::NaturalLog:
        return "ln";
    case Function::CommonLog:
        return "log";
    case Function::Percent:
        return nullptr;
    }
    return nullptr;
}

const char* functionName(Function function)
{
    switch (function)
    {
    case Function::Square:
        return "x^2";
    case Function::Reciprocal:
        return "1/x";
    case Function::Percent:
        return "%";
    default:
        return functionToken(function);
    }
}

double degreesToRadians(double value)
{
    return value * kPi / 180.0;
}

double radiansToDegrees(double value)
{
    return value * 180.0 / kPi;
}

bool evaluateFunction(Function function, AngleMode angle_mode, double input, double* output)
{
    if (output == nullptr)
    {
        return false;
    }

    const double angle = angle_mode == AngleMode::Degrees ? degreesToRadians(input) : input;
    switch (function)
    {
    case Function::Sin:
        *output = std::sin(angle);
        break;
    case Function::Cos:
        *output = std::cos(angle);
        break;
    case Function::Tan:
        if (std::fabs(std::cos(angle)) < kDomainEpsilon)
        {
            return false;
        }
        *output = std::tan(angle);
        break;
    case Function::Asin:
        if (input < -1.0 || input > 1.0)
        {
            return false;
        }
        *output = std::asin(input);
        if (angle_mode == AngleMode::Degrees)
        {
            *output = radiansToDegrees(*output);
        }
        break;
    case Function::Acos:
        if (input < -1.0 || input > 1.0)
        {
            return false;
        }
        *output = std::acos(input);
        if (angle_mode == AngleMode::Degrees)
        {
            *output = radiansToDegrees(*output);
        }
        break;
    case Function::Atan:
        *output = std::atan(input);
        if (angle_mode == AngleMode::Degrees)
        {
            *output = radiansToDegrees(*output);
        }
        break;
    case Function::Square:
        *output = input * input;
        break;
    case Function::SquareRoot:
        if (input < 0.0)
        {
            return false;
        }
        *output = std::sqrt(input);
        break;
    case Function::Reciprocal:
        if (std::fabs(input) < kDomainEpsilon)
        {
            return false;
        }
        *output = 1.0 / input;
        break;
    case Function::NaturalLog:
        if (input <= 0.0)
        {
            return false;
        }
        *output = std::log(input);
        break;
    case Function::CommonLog:
        if (input <= 0.0)
        {
            return false;
        }
        *output = std::log10(input);
        break;
    case Function::Percent:
        *output = input / 100.0;
        break;
    }
    return isFinite(*output);
}

bool identifierEquals(const char* start, size_t length, const char* text)
{
    return std::strlen(text) == length && std::strncmp(start, text, length) == 0;
}

class ExpressionParser
{
  public:
    ExpressionParser(const char* expression, double answer, AngleMode angle_mode)
        : expression_(expression ? expression : ""), answer_(answer), angle_mode_(angle_mode)
    {
    }

    bool parse(double* output)
    {
        if (output == nullptr || !parseExpression(output))
        {
            return false;
        }
        skipSpaces();
        return expression_[position_] == '\0' && isFinite(*output);
    }

  private:
    const char* expression_;
    double answer_;
    AngleMode angle_mode_;
    size_t position_ = 0;

    void skipSpaces()
    {
        while (std::isspace(static_cast<unsigned char>(expression_[position_])) != 0)
        {
            ++position_;
        }
    }

    bool consume(char token)
    {
        skipSpaces();
        if (expression_[position_] != token)
        {
            return false;
        }
        ++position_;
        return true;
    }

    bool parseExpression(double* output)
    {
        if (!parseTerm(output))
        {
            return false;
        }
        while (true)
        {
            if (consume('+'))
            {
                double rhs = 0.0;
                if (!parseTerm(&rhs))
                {
                    return false;
                }
                *output += rhs;
            }
            else if (consume('-'))
            {
                double rhs = 0.0;
                if (!parseTerm(&rhs))
                {
                    return false;
                }
                *output -= rhs;
            }
            else
            {
                return isFinite(*output);
            }
        }
    }

    bool parseTerm(double* output)
    {
        if (!parseUnary(output))
        {
            return false;
        }
        while (true)
        {
            skipSpaces();
            const char token = expression_[position_];
            if (token != '*' && token != 'x' && token != '/')
            {
                return isFinite(*output);
            }
            ++position_;
            double rhs = 0.0;
            if (!parseUnary(&rhs))
            {
                return false;
            }
            if (token == '/')
            {
                if (std::fabs(rhs) < kDomainEpsilon)
                {
                    return false;
                }
                *output /= rhs;
            }
            else
            {
                *output *= rhs;
            }
            if (!isFinite(*output))
            {
                return false;
            }
        }
    }

    bool parseUnary(double* output)
    {
        if (consume('+'))
        {
            return parseUnary(output);
        }
        if (consume('-'))
        {
            if (!parseUnary(output))
            {
                return false;
            }
            *output = -*output;
            return true;
        }
        return parsePower(output);
    }

    bool parsePower(double* output)
    {
        if (!parsePostfix(output))
        {
            return false;
        }
        if (!consume('^'))
        {
            return true;
        }
        double exponent = 0.0;
        if (!parseUnary(&exponent))
        {
            return false;
        }
        *output = std::pow(*output, exponent);
        return isFinite(*output);
    }

    bool parsePostfix(double* output)
    {
        if (!parsePrimary(output))
        {
            return false;
        }
        while (consume('%'))
        {
            *output /= 100.0;
        }
        return isFinite(*output);
    }

    bool parsePrimary(double* output)
    {
        skipSpaces();
        const char token = expression_[position_];
        if (token == '(')
        {
            ++position_;
            if (!parseExpression(output) || !consume(')'))
            {
                return false;
            }
            return true;
        }
        if (std::isdigit(static_cast<unsigned char>(token)) != 0 || token == '.')
        {
            char* end = nullptr;
            *output = std::strtod(expression_ + position_, &end);
            if (end == expression_ + position_ || !isFinite(*output))
            {
                return false;
            }
            position_ = static_cast<size_t>(end - expression_);
            return true;
        }
        if (std::isalpha(static_cast<unsigned char>(token)) == 0)
        {
            return false;
        }

        const size_t start = position_;
        while (std::isalpha(static_cast<unsigned char>(expression_[position_])) != 0)
        {
            ++position_;
        }
        const size_t length = position_ - start;
        const char* identifier = expression_ + start;
        if (identifierEquals(identifier, length, "pi"))
        {
            *output = kPi;
            return true;
        }
        if (identifierEquals(identifier, length, "ans"))
        {
            *output = answer_;
            return true;
        }

        Function function = Function::Sin;
        if (identifierEquals(identifier, length, "sin"))
        {
            function = Function::Sin;
        }
        else if (identifierEquals(identifier, length, "cos"))
        {
            function = Function::Cos;
        }
        else if (identifierEquals(identifier, length, "tan"))
        {
            function = Function::Tan;
        }
        else if (identifierEquals(identifier, length, "asin"))
        {
            function = Function::Asin;
        }
        else if (identifierEquals(identifier, length, "acos"))
        {
            function = Function::Acos;
        }
        else if (identifierEquals(identifier, length, "atan"))
        {
            function = Function::Atan;
        }
        else if (identifierEquals(identifier, length, "sqr"))
        {
            function = Function::Square;
        }
        else if (identifierEquals(identifier, length, "sqrt"))
        {
            function = Function::SquareRoot;
        }
        else if (identifierEquals(identifier, length, "inv"))
        {
            function = Function::Reciprocal;
        }
        else if (identifierEquals(identifier, length, "ln"))
        {
            function = Function::NaturalLog;
        }
        else if (identifierEquals(identifier, length, "log"))
        {
            function = Function::CommonLog;
        }
        else
        {
            return false;
        }

        double input = 0.0;
        if (!consume('(') || !parseExpression(&input) || !consume(')'))
        {
            return false;
        }
        return evaluateFunction(function, angle_mode_, input, output);
    }
};

} // namespace

Engine::Engine()
{
    allClear();
}

void Engine::allClear()
{
    value_ = 0.0;
    answer_ = 0.0;
    second_layer_ = false;
    error_ = false;
    showing_result_ = false;
    replace_on_next_input_ = true;
    expression_length_ = 1;
    cursor_ = 1;
    std::snprintf(expression_, sizeof(expression_), "%s", "0");
    std::snprintf(history_, sizeof(history_), "%s", "READY");
}

void Engine::clearEntry()
{
    error_ = false;
    showing_result_ = false;
    replace_on_next_input_ = true;
    expression_length_ = 1;
    cursor_ = 1;
    std::snprintf(expression_, sizeof(expression_), "%s", "0");
    std::snprintf(history_, sizeof(history_), "%s", "ENTRY CLEARED");
}

void Engine::prepareForInsertion()
{
    if (error_)
    {
        clearEntry();
    }
    if (showing_result_)
    {
        showing_result_ = false;
        replace_on_next_input_ = true;
    }
    if (replace_on_next_input_)
    {
        expression_length_ = 0;
        cursor_ = 0;
        expression_[0] = '\0';
        replace_on_next_input_ = false;
    }
}

bool Engine::insertText(const char* text)
{
    if (text == nullptr)
    {
        return false;
    }
    prepareForInsertion();
    const size_t text_length = std::strlen(text);
    if (text_length == 0U)
    {
        return true;
    }
    if (expression_length_ + text_length >= sizeof(expression_))
    {
        return false;
    }
    std::memmove(expression_ + cursor_ + text_length,
                 expression_ + cursor_,
                 expression_length_ - cursor_ + 1U);
    std::memcpy(expression_ + cursor_, text, text_length);
    expression_length_ += text_length;
    cursor_ += text_length;
    return true;
}

void Engine::eraseRange(size_t offset, size_t length)
{
    if (offset >= expression_length_ || length == 0U)
    {
        return;
    }
    const size_t erased = length > expression_length_ - offset ? expression_length_ - offset : length;
    std::memmove(expression_ + offset, expression_ + offset + erased, expression_length_ - offset - erased + 1U);
    expression_length_ -= erased;
    if (cursor_ > expression_length_)
    {
        cursor_ = expression_length_;
    }
}

void Engine::backspace()
{
    if (error_)
    {
        clearEntry();
        return;
    }
    if (showing_result_)
    {
        showing_result_ = false;
        replace_on_next_input_ = false;
    }
    if (cursor_ == 0U)
    {
        return;
    }

    const size_t erase_at = cursor_ - 1U;
    if (expression_[erase_at] == '(' && cursor_ < expression_length_ && expression_[cursor_] == ')')
    {
        eraseRange(erase_at, 2U);
        cursor_ = erase_at;
        while (cursor_ > 0U && std::isalpha(static_cast<unsigned char>(expression_[cursor_ - 1U])) != 0)
        {
            --cursor_;
        }
        eraseRange(cursor_, erase_at - cursor_);
        return;
    }

    eraseRange(erase_at, 1U);
    cursor_ = erase_at;
}

void Engine::moveCursorLeft()
{
    if (error_)
    {
        return;
    }
    showing_result_ = false;
    replace_on_next_input_ = false;
    if (cursor_ > 0U)
    {
        --cursor_;
    }
}

void Engine::moveCursorRight()
{
    if (error_)
    {
        return;
    }
    showing_result_ = false;
    replace_on_next_input_ = false;
    if (cursor_ < expression_length_)
    {
        ++cursor_;
    }
}

void Engine::inputDigit(char digit)
{
    if (digit < '0' || digit > '9')
    {
        return;
    }
    const char text[] = {digit, '\0'};
    (void)insertText(text);
}

void Engine::inputDecimalPoint()
{
    prepareForInsertion();
    for (size_t position = cursor_; position > 0U; --position)
    {
        const char token = expression_[position - 1U];
        if (token == '.')
        {
            return;
        }
        if (std::isdigit(static_cast<unsigned char>(token)) == 0)
        {
            break;
        }
    }
    if (cursor_ == 0U || std::isdigit(static_cast<unsigned char>(expression_[cursor_ - 1U])) == 0)
    {
        (void)insertText("0.");
        return;
    }
    (void)insertText(".");
}

void Engine::inputPi()
{
    (void)insertText("pi");
}

void Engine::inputAnswer()
{
    (void)insertText("ans");
}

void Engine::inputPercent()
{
    (void)insertText("%");
}

void Engine::insertPaired(const char* prefix)
{
    if (prefix != nullptr && prefix[0] != '\0' && !insertText(prefix))
    {
        return;
    }
    if (insertText("()"))
    {
        moveCursorLeft();
    }
}

void Engine::insertOpenParenthesis()
{
    insertPaired("");
}

void Engine::insertCloseParenthesis()
{
    prepareForInsertion();
    if (cursor_ < expression_length_ && expression_[cursor_] == ')')
    {
        ++cursor_;
        return;
    }
    (void)insertText(")");
}

void Engine::toggleSign()
{
    prepareForInsertion();
    if (cursor_ > 0U && expression_[cursor_ - 1U] == '-')
    {
        eraseRange(cursor_ - 1U, 1U);
        --cursor_;
        return;
    }
    (void)insertText("-");
}

void Engine::selectOperation(Operation operation)
{
    (void)insertText(operationSymbol(operation));
}

void Engine::equals()
{
    double result = 0.0;
    if (!evaluateExpression(&result))
    {
        setError("SYNTAX OR MATH ERROR");
        return;
    }
    value_ = result;
    answer_ = result;
    showing_result_ = true;
    replace_on_next_input_ = false;
}

void Engine::beginFunction(Function function)
{
    const char* token = functionToken(function);
    if (token == nullptr)
    {
        inputPercent();
        return;
    }
    insertPaired(token);
}

void Engine::setExpressionToValue(double value)
{
    formatValue(value, expression_, sizeof(expression_));
    expression_length_ = std::strlen(expression_);
    cursor_ = expression_length_;
    replace_on_next_input_ = false;
}

void Engine::apply(Function function)
{
    double input = value_;
    double output = 0.0;
    if ((!showing_result_ && !evaluateExpression(&input)) ||
        !evaluateFunction(function, angle_mode_, input, &output))
    {
        setError("SYNTAX OR MATH ERROR");
        return;
    }
    value_ = output;
    answer_ = output;
    setExpressionToValue(output);
    showing_result_ = true;
    std::snprintf(history_, sizeof(history_), "%s(%.10g)", functionName(function), input);
}

void Engine::toggleAngleMode()
{
    angle_mode_ = angle_mode_ == AngleMode::Degrees ? AngleMode::Radians : AngleMode::Degrees;
}

void Engine::toggleSecondLayer()
{
    second_layer_ = !second_layer_;
}

const char* Engine::displayText() const
{
    if (error_)
    {
        return "ERROR";
    }
    if (showing_result_)
    {
        formatValue(value_, display_, sizeof(display_));
        return display_;
    }

    const size_t before_cursor = cursor_ > expression_length_ ? expression_length_ : cursor_;
    std::memcpy(display_, expression_, before_cursor);
    display_[before_cursor] = '|';
    std::memcpy(display_ + before_cursor + 1U,
                expression_ + before_cursor,
                expression_length_ - before_cursor + 1U);
    return display_;
}

const char* Engine::historyText() const
{
    if (error_)
    {
        return history_;
    }
    if (showing_result_)
    {
        std::snprintf(history_, sizeof(history_), "%s =", expression_);
        return history_;
    }
    return "EDIT  |  < > MOVE  |  = CALCULATE";
}

const char* Engine::angleModeText() const
{
    return angle_mode_ == AngleMode::Degrees ? "DEG" : "RAD";
}

bool Engine::secondLayer() const
{
    return second_layer_;
}

bool Engine::hasError() const
{
    return error_;
}

double Engine::value() const
{
    if (error_ || showing_result_)
    {
        return value_;
    }
    double evaluated = 0.0;
    return evaluateExpression(&evaluated) ? evaluated : value_;
}

void Engine::setError(const char* message)
{
    error_ = true;
    showing_result_ = false;
    replace_on_next_input_ = true;
    std::snprintf(history_, sizeof(history_), "%s", message ? message : "SYNTAX OR MATH ERROR");
}

bool Engine::evaluateExpression(double* output) const
{
    ExpressionParser parser(expression_, answer_, angle_mode_);
    return parser.parse(output);
}

} // namespace calculator::ui::model
