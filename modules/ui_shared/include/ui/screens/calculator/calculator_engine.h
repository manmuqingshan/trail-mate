#pragma once

#include <cstdint>

namespace calculator::ui::model
{

enum class AngleMode : uint8_t
{
    Degrees = 0,
    Radians,
};

enum class Operation : uint8_t
{
    Add = 0,
    Subtract,
    Multiply,
    Divide,
    Power,
};

enum class Function : uint8_t
{
    Sin = 0,
    Cos,
    Tan,
    Asin,
    Acos,
    Atan,
    Square,
    SquareRoot,
    Reciprocal,
    NaturalLog,
    CommonLog,
    Percent,
};

// Fixed-size calculator state intended for an embedded page.  It deliberately
// has no dynamic allocation: the keypad can be used while radio and map tasks
// are active without adding heap churn to the Pager UI.
class Engine
{
  public:
    Engine();

    void allClear();
    void clearEntry();
    void backspace();
    void inputDigit(char digit);
    void inputDecimalPoint();
    void inputPi();
    void inputAnswer();
    void toggleSign();
    void selectOperation(Operation operation);
    void equals();
    // Starts an editable function argument when no number is being entered.
    // If the user has already entered a number, the function is applied to it
    // immediately, which supports both familiar scientific-calculator flows.
    void beginFunction(Function function);
    void apply(Function function);
    void toggleAngleMode();
    void toggleSecondLayer();

    const char* displayText() const;
    const char* historyText() const;
    const char* angleModeText() const;
    bool secondLayer() const;
    bool hasError() const;
    double value() const;

  private:
    static constexpr uint8_t kEntryCapacity = 24;
    static constexpr uint8_t kDisplayCapacity = 32;
    static constexpr uint8_t kHistoryCapacity = 48;

    double accumulator_ = 0.0;
    double value_ = 0.0;
    double answer_ = 0.0;
    Operation pending_operation_ = Operation::Add;
    Function pending_function_ = Function::Sin;
    AngleMode angle_mode_ = AngleMode::Degrees;
    bool has_pending_operation_ = false;
    bool has_pending_function_ = false;
    bool entering_ = false;
    bool replace_entry_ = true;
    bool after_equals_ = false;
    bool second_layer_ = false;
    bool error_ = false;
    char entry_[kEntryCapacity]{};
    mutable char display_[kDisplayCapacity]{};
    mutable char history_[kHistoryCapacity]{};

    double currentValue() const;
    void setEntryValue(double value);
    void setError(const char* message);
    bool resolvePending(double operand);
    bool commitPendingFunction();
    bool applyFunction(Function function, double input, double* output) const;
    void beginEntryIfNeeded();
};

} // namespace calculator::ui::model
