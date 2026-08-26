#include "ui/screens/calculator/calculator_engine.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace calculator::ui::model
{
namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kDomainEpsilon = 1.0e-10;

const char* operationSymbol(Operation operation)
{
    switch (operation)
    {
    case Operation::Add:
        return "+";
    case Operation::Subtract:
        return "-";
    case Operation::Multiply:
        return "x";
    case Operation::Divide:
        return "/";
    case Operation::Power:
        return "^";
    }
    return "?";
}

const char* functionName(Function function)
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
        return "x^2";
    case Function::SquareRoot:
        return "sqrt";
    case Function::Reciprocal:
        return "1/x";
    case Function::NaturalLog:
        return "ln";
    case Function::CommonLog:
        return "log";
    case Function::Percent:
        return "%";
    }
    return "fn";
}

double degreesToRadians(double value)
{
    return value * kPi / 180.0;
}

double radiansToDegrees(double value)
{
    return value * 180.0 / kPi;
}

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

} // namespace

Engine::Engine()
{
    allClear();
}

void Engine::allClear()
{
    accumulator_ = 0.0;
    value_ = 0.0;
    answer_ = 0.0;
    pending_operation_ = Operation::Add;
    has_pending_operation_ = false;
    entering_ = false;
    replace_entry_ = true;
    after_equals_ = false;
    second_layer_ = false;
    error_ = false;
    std::snprintf(entry_, sizeof(entry_), "%s", "0");
    std::snprintf(history_, sizeof(history_), "%s", "READY");
}

void Engine::clearEntry()
{
    if (error_)
    {
        allClear();
        return;
    }
    entering_ = false;
    replace_entry_ = true;
    after_equals_ = false;
    std::snprintf(entry_, sizeof(entry_), "%s", "0");
    std::snprintf(history_, sizeof(history_), "%s", "ENTRY CLEARED");
}

void Engine::backspace()
{
    if (error_)
    {
        allClear();
        return;
    }
    if (!entering_ || replace_entry_)
    {
        clearEntry();
        return;
    }

    size_t length = std::strlen(entry_);
    if (length <= 1U || (length == 2U && entry_[0] == '-'))
    {
        clearEntry();
        return;
    }
    entry_[length - 1U] = '\0';
}

void Engine::beginEntryIfNeeded()
{
    if (error_)
    {
        allClear();
    }
    if (!entering_ || replace_entry_)
    {
        if (after_equals_)
        {
            has_pending_operation_ = false;
            after_equals_ = false;
        }
        std::snprintf(entry_, sizeof(entry_), "%s", "");
        entering_ = true;
        replace_entry_ = false;
    }
}

void Engine::inputDigit(char digit)
{
    if (digit < '0' || digit > '9')
    {
        return;
    }
    beginEntryIfNeeded();
    const size_t length = std::strlen(entry_);
    if (length + 1U >= sizeof(entry_))
    {
        return;
    }
    if (length == 0U || (length == 1U && entry_[0] == '0'))
    {
        entry_[0] = digit;
        entry_[1] = '\0';
    }
    else if (length == 2U && entry_[0] == '-' && entry_[1] == '0')
    {
        entry_[1] = digit;
        entry_[2] = '\0';
    }
    else
    {
        entry_[length] = digit;
        entry_[length + 1U] = '\0';
    }
}

void Engine::inputDecimalPoint()
{
    beginEntryIfNeeded();
    if (std::strchr(entry_, '.') != nullptr)
    {
        return;
    }
    const size_t length = std::strlen(entry_);
    if (length + 2U >= sizeof(entry_))
    {
        return;
    }
    if (length == 0U)
    {
        std::snprintf(entry_, sizeof(entry_), "%s", "0.");
    }
    else if (length == 1U && entry_[0] == '-')
    {
        std::snprintf(entry_, sizeof(entry_), "%s", "-0.");
    }
    else
    {
        entry_[length] = '.';
        entry_[length + 1U] = '\0';
    }
}

void Engine::setEntryValue(double value)
{
    formatValue(value, entry_, sizeof(entry_));
    entering_ = true;
    replace_entry_ = false;
    after_equals_ = false;
}

void Engine::inputPi()
{
    if (error_)
    {
        allClear();
    }
    setEntryValue(kPi);
    std::snprintf(history_, sizeof(history_), "%s", "PI");
}

void Engine::inputAnswer()
{
    if (error_)
    {
        allClear();
    }
    setEntryValue(answer_);
    std::snprintf(history_, sizeof(history_), "%s", "ANS");
}

void Engine::toggleSign()
{
    if (error_)
    {
        allClear();
    }
    if (!entering_ || replace_entry_)
    {
        setEntryValue(currentValue());
    }
    if (entry_[0] == '-')
    {
        std::memmove(entry_, entry_ + 1, std::strlen(entry_));
    }
    else
    {
        const size_t length = std::strlen(entry_);
        if (length + 1U >= sizeof(entry_))
        {
            return;
        }
        std::memmove(entry_ + 1, entry_, length + 1U);
        entry_[0] = '-';
    }
}

double Engine::currentValue() const
{
    if (entering_ && !replace_entry_)
    {
        return std::strtod(entry_, nullptr);
    }
    return value_;
}

bool Engine::resolvePending(double operand)
{
    double result = accumulator_;
    switch (pending_operation_)
    {
    case Operation::Add:
        result += operand;
        break;
    case Operation::Subtract:
        result -= operand;
        break;
    case Operation::Multiply:
        result *= operand;
        break;
    case Operation::Divide:
        if (std::fabs(operand) < kDomainEpsilon)
        {
            setError("DIVIDE BY ZERO");
            return false;
        }
        result /= operand;
        break;
    case Operation::Power:
        result = std::pow(result, operand);
        break;
    }
    if (!isFinite(result))
    {
        setError("MATH ERROR");
        return false;
    }
    accumulator_ = result;
    value_ = result;
    return true;
}

void Engine::setError(const char* message)
{
    error_ = true;
    has_pending_operation_ = false;
    entering_ = false;
    replace_entry_ = true;
    after_equals_ = false;
    std::snprintf(history_, sizeof(history_), "%s", message ? message : "MATH ERROR");
}

void Engine::selectOperation(Operation operation)
{
    if (error_)
    {
        return;
    }
    const double operand = currentValue();
    if (has_pending_operation_ && entering_ && !resolvePending(operand))
    {
        return;
    }
    if (!has_pending_operation_)
    {
        accumulator_ = operand;
        value_ = operand;
    }
    pending_operation_ = operation;
    has_pending_operation_ = true;
    entering_ = false;
    replace_entry_ = true;
    after_equals_ = false;
    char value_text[kDisplayCapacity]{};
    formatValue(value_, value_text, sizeof(value_text));
    std::snprintf(history_, sizeof(history_), "%s %s", value_text, operationSymbol(operation));
}

void Engine::equals()
{
    if (error_ || !has_pending_operation_)
    {
        return;
    }
    if (!resolvePending(currentValue()))
    {
        return;
    }
    has_pending_operation_ = false;
    entering_ = false;
    replace_entry_ = true;
    after_equals_ = true;
    answer_ = value_;
    std::snprintf(history_, sizeof(history_), "%s", "RESULT");
}

bool Engine::applyFunction(Function function, double input, double* output) const
{
    if (output == nullptr)
    {
        return false;
    }

    const double angle = angle_mode_ == AngleMode::Degrees ? degreesToRadians(input) : input;
    switch (function)
    {
    case Function::Sin:
        *output = std::sin(angle);
        return true;
    case Function::Cos:
        *output = std::cos(angle);
        return true;
    case Function::Tan:
        if (std::fabs(std::cos(angle)) < kDomainEpsilon)
        {
            return false;
        }
        *output = std::tan(angle);
        return true;
    case Function::Asin:
        if (input < -1.0 || input > 1.0)
        {
            return false;
        }
        *output = std::asin(input);
        break;
    case Function::Acos:
        if (input < -1.0 || input > 1.0)
        {
            return false;
        }
        *output = std::acos(input);
        break;
    case Function::Atan:
        *output = std::atan(input);
        break;
    case Function::Square:
        *output = input * input;
        return true;
    case Function::SquareRoot:
        if (input < 0.0)
        {
            return false;
        }
        *output = std::sqrt(input);
        return true;
    case Function::Reciprocal:
        if (std::fabs(input) < kDomainEpsilon)
        {
            return false;
        }
        *output = 1.0 / input;
        return true;
    case Function::NaturalLog:
        if (input <= 0.0)
        {
            return false;
        }
        *output = std::log(input);
        return true;
    case Function::CommonLog:
        if (input <= 0.0)
        {
            return false;
        }
        *output = std::log10(input);
        return true;
    case Function::Percent:
        *output = input / 100.0;
        return true;
    }

    if (angle_mode_ == AngleMode::Degrees)
    {
        *output = radiansToDegrees(*output);
    }
    return isFinite(*output);
}

void Engine::apply(Function function)
{
    if (error_)
    {
        return;
    }
    const double input = currentValue();
    double output = 0.0;
    if (!applyFunction(function, input, &output) || !isFinite(output))
    {
        setError("MATH ERROR");
        return;
    }
    char input_text[kDisplayCapacity]{};
    formatValue(input, input_text, sizeof(input_text));
    std::snprintf(history_, sizeof(history_), "%s(%s)", functionName(function), input_text);
    setEntryValue(output);
}

void Engine::toggleAngleMode()
{
    angle_mode_ = angle_mode_ == AngleMode::Degrees ? AngleMode::Radians : AngleMode::Degrees;
    std::snprintf(history_, sizeof(history_), "ANGLE MODE: %s", angleModeText());
}

void Engine::toggleSecondLayer()
{
    second_layer_ = !second_layer_;
    std::snprintf(history_, sizeof(history_), "%s", second_layer_ ? "2ND FUNCTIONS" : "PRIMARY FUNCTIONS");
}

const char* Engine::displayText() const
{
    if (error_)
    {
        return "ERROR";
    }
    if (entering_ && !replace_entry_)
    {
        return entry_;
    }
    formatValue(value_, display_, sizeof(display_));
    return display_;
}

const char* Engine::historyText() const
{
    return history_;
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
    return currentValue();
}

} // namespace calculator::ui::model
