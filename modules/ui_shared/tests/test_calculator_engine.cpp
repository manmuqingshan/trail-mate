#include "ui/screens/calculator/calculator_engine.h"

#include <cassert>
#include <cmath>
#include <cstring>

namespace
{

constexpr double kPi = 3.14159265358979323846;

void expectNear(double actual, double expected, double epsilon = 1.0e-9)
{
    assert(std::fabs(actual - expected) <= epsilon);
}

void enterNumber(calculator::ui::model::Engine& engine, const char* digits)
{
    for (const char* cursor = digits; *cursor; ++cursor)
    {
        if (*cursor == '.')
        {
            engine.inputDecimalPoint();
        }
        else
        {
            engine.inputDigit(*cursor);
        }
    }
}

} // namespace

int main()
{
    using calculator::ui::model::Engine;
    using calculator::ui::model::Function;
    using calculator::ui::model::Operation;

    Engine engine;

    // A function key creates an editable paired expression, not tan(0).
    engine.beginFunction(Function::Tan);
    assert(std::strcmp(engine.displayText(), "tan(|)") == 0);
    enterNumber(engine, "75");
    assert(std::strcmp(engine.displayText(), "tan(75|)") == 0);
    engine.moveCursorRight();
    engine.selectOperation(Operation::Add);
    engine.beginFunction(Function::Sin);
    enterNumber(engine, "90");
    assert(std::strcmp(engine.displayText(), "tan(75)+sin(90|)") == 0);
    engine.equals();
    expectNear(engine.value(), std::tan(75.0 * kPi / 180.0) + 1.0, 1.0e-8);
    assert(std::strcmp(engine.historyText(), "tan(75)+sin(90) =") == 0);

    // The paired closing parenthesis lets a compound argument be completed
    // directly with = while the cursor remains inside the function call.
    engine.allClear();
    engine.beginFunction(Function::Tan);
    enterNumber(engine, "75");
    engine.selectOperation(Operation::Add);
    enterNumber(engine, "1");
    engine.selectOperation(Operation::Divide);
    enterNumber(engine, "3");
    engine.equals();
    expectNear(engine.value(), std::tan((75.0 + 1.0 / 3.0) * kPi / 180.0), 1.0e-8);

    // Cursor movement inserts into an existing number rather than appending.
    engine.allClear();
    enterNumber(engine, "13");
    engine.moveCursorLeft();
    engine.inputDigit('2');
    assert(std::strcmp(engine.displayText(), "12|3") == 0);
    engine.equals();
    expectNear(engine.value(), 123.0);

    // Explicit grouping uses the same paired-parenthesis interaction.
    engine.allClear();
    engine.insertOpenParenthesis();
    enterNumber(engine, "2");
    engine.selectOperation(Operation::Add);
    enterNumber(engine, "3");
    engine.insertCloseParenthesis();
    engine.selectOperation(Operation::Multiply);
    enterNumber(engine, "4");
    engine.equals();
    expectNear(engine.value(), 20.0);

    // Backspace removes an empty auto-paired function call as one unit.
    engine.allClear();
    engine.beginFunction(Function::Sin);
    engine.backspace();
    assert(std::strcmp(engine.displayText(), "|") == 0);
    engine.inputDigit('2');
    engine.equals();
    expectNear(engine.value(), 2.0);

    // Direct apply remains available for programmatic clients and quick tests.
    engine.allClear();
    enterNumber(engine, "30");
    engine.apply(Function::Sin);
    expectNear(engine.value(), 0.5);
    engine.apply(Function::Asin);
    expectNear(engine.value(), 30.0, 1.0e-8);

    engine.allClear();
    engine.toggleAngleMode();
    engine.inputPi();
    engine.selectOperation(Operation::Divide);
    enterNumber(engine, "2");
    engine.equals();
    engine.apply(Function::Sin);
    expectNear(engine.value(), 1.0, 1.0e-8);
    assert(std::strcmp(engine.angleModeText(), "RAD") == 0);

    engine.allClear();
    enterNumber(engine, "50");
    engine.inputPercent();
    engine.equals();
    expectNear(engine.value(), 0.5);

    engine.clearEntry();
    engine.inputAnswer();
    engine.selectOperation(Operation::Add);
    engine.inputPi();
    engine.equals();
    expectNear(engine.value(), 0.5 + kPi, 1.0e-8);

    engine.clearEntry();
    engine.inputDigit('1');
    engine.selectOperation(Operation::Divide);
    engine.inputDigit('0');
    engine.equals();
    assert(engine.hasError());
    assert(std::strcmp(engine.displayText(), "ERROR") == 0);

    return 0;
}
