#pragma once

#include <cstddef>
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

// Fixed-size scientific expression editor for embedded pages. It deliberately
// avoids heap allocation so the calculator can run beside radio and map tasks
// without adding allocation churn to the Pager UI.
class Engine
{
  public:
    Engine();

    void allClear();
    void clearEntry();
    void backspace();
    void moveCursorLeft();
    void moveCursorRight();
    void inputDigit(char digit);
    void inputDecimalPoint();
    void inputPi();
    void inputAnswer();
    void inputPercent();
    void insertOpenParenthesis();
    void insertCloseParenthesis();
    void toggleSign();
    void selectOperation(Operation operation);
    void equals();

    // Inserts an editable function call such as tan() and leaves the cursor
    // inside its parentheses. It does not evaluate until = is pressed.
    void beginFunction(Function function);

    // Keeps a compact direct-apply operation for non-text input clients and
    // engine tests. The calculator page itself uses beginFunction().
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
    static constexpr size_t kExpressionCapacity = 72;
    static constexpr size_t kDisplayCapacity = kExpressionCapacity + 12;
    static constexpr size_t kHistoryCapacity = kExpressionCapacity + 16;

    double value_ = 0.0;
    double answer_ = 0.0;
    AngleMode angle_mode_ = AngleMode::Degrees;
    bool second_layer_ = false;
    bool error_ = false;
    bool showing_result_ = false;
    bool replace_on_next_input_ = true;
    size_t expression_length_ = 1;
    size_t cursor_ = 1;
    char expression_[kExpressionCapacity] = "0";
    mutable char display_[kDisplayCapacity]{};
    mutable char history_[kHistoryCapacity]{};

    void prepareForInsertion();
    bool insertText(const char* text);
    void insertPaired(const char* prefix);
    void eraseRange(size_t offset, size_t length);
    void setExpressionToValue(double value);
    void setError(const char* message);
    bool evaluateExpression(double* output) const;
};

} // namespace calculator::ui::model
