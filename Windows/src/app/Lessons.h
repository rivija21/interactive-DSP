#pragma once
#include <string>
#include <vector>

/// Short lessons shown in the Theory panel. Paragraph markup: lines starting with "• " are
/// bullets, "= " lines are equations, "Try: " lines are hints.
struct Lesson {
    std::string id;
    std::string title;
    std::string summary;
    std::vector<std::string> body;
    std::vector<std::string> experiments;
};

namespace Lessons {
const std::vector<Lesson>& all();
const Lesson* lesson(const std::string& id);
} // namespace Lessons

/// Guided experiments: each sets up the lab in one click.
struct Experiment {
    std::string id;
    std::string title;
    std::string description;
};

namespace Experiments {
const std::vector<Experiment>& all();
const Experiment* experiment(const std::string& id);
} // namespace Experiments
