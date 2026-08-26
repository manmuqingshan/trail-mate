#include "ui/screens/calculator/calculator_engine.h"

#include <cassert>
#include <cmath>
#include <cstring>

namespace
{

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
    enterNumber(engine, "12.5");
    engine.selectOperation(Operation::Add);
    enterNumber(engine, "7.5");
    engine.equals();
    expectNear(engine.value(), 20.0);

    engine.allClear();
    enterNumber(engine, "2");
    engine.selectOperation(Operation::Power);
    enterNumber(engine, "8");
    engine.equals();
    expectNear(engine.value(), 256.0);

    engine.allClear();
    enterNumber(engine, "30");
    engine.apply(Function::Sin);
    expectNear(engine.value(), 0.5);
    engine.apply(Function::Asin);
    expectNear(engine.value(), 30.0, 1.0e-8);

    engine.allClear();
    engine.beginFunction(Function::Tan);
    assert(std::strcmp(engine.historyText(), "tan(") == 0);
    enterNumber(engine, "75");
    assert(std::strcmp(engine.historyText(), "tan(75") == 0);
    engine.equals();
    expectNear(engine.value(), 3.732050807568877, 1.0e-8);
    assert(std::strcmp(engine.historyText(), "tan(75)") == 0);

    engine.allClear();
    enterNumber(engine, "45");
    engine.beginFunction(Function::Tan);
    expectNear(engine.value(), 1.0, 1.0e-8);
    assert(std::strcmp(engine.historyText(), "tan(45)") == 0);

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
    enterNumber(engine, "100");
    engine.apply(Function::CommonLog);
    expectNear(engine.value(), 2.0);
    engine.apply(Function::Square);
    expectNear(engine.value(), 4.0);
    engine.apply(Function::SquareRoot);
    expectNear(engine.value(), 2.0);

    engine.allClear();
    enterNumber(engine, "1");
    engine.selectOperation(Operation::Divide);
    enterNumber(engine, "0");
    engine.equals();
    assert(engine.hasError());
    assert(std::strcmp(engine.displayText(), "ERROR") == 0);
    engine.allClear();
    engine.inputPi();
    const double pi = engine.value();
    engine.inputAnswer();
    expectNear(engine.value(), 0.0);
    engine.selectOperation(Operation::Add);
    engine.inputPi();
    engine.equals();
    expectNear(engine.value(), pi);

    return 0;
}
