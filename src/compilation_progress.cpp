#include "../include/compilation_progress.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <iostream>
#include <sstream>
#include <iomanip>

using namespace ftxui;

CompilationProgress::CompilationProgress(bool enableAnimation) 
    : enableAnimation_(enableAnimation), liveUpdates_(true), running_(false), 
      currentStage_(Stage::LEXING), overallProgress_(0.0f) {
    
    // Initialize stage information
    stages_[Stage::LEXING] = {"Lexical Analysis", "✓", false, false, 0.0f};
    stages_[Stage::PARSING] = {"Parsing", "✓", false, false, 0.0f};
    stages_[Stage::CODE_GENERATION] = {"Code Generation", "✓", false, false, 0.0f};
    stages_[Stage::OPTIMIZATION] = {"Optimization", "✓", false, false, 0.0f};
    stages_[Stage::LINKING] = {"Linking", "✓", false, false, 0.0f};
    stages_[Stage::COMPLETE] = {"Complete", "✓", false, false, 0.0f};
}

CompilationProgress::~CompilationProgress() {
    stop();
}

void CompilationProgress::start(const std::string& filename) {
    filename_ = filename;
    startTime_ = std::chrono::steady_clock::now();
    running_ = true;
    currentStage_ = Stage::LEXING;
    stages_[currentStage_].current = true;
    
    if (liveUpdates_ && enableAnimation_) {
                std::cout << std::endl;
        renderThread_ = std::make_unique<std::thread>([this]() { renderLoop(); });
    }
}

void CompilationProgress::stop() {
    running_ = false;
    if (renderThread_ && renderThread_->joinable()) {
        renderThread_->join();
    }
}

void CompilationProgress::setStage(Stage stage) {
    if (currentStage_ != stage) {
        stages_[currentStage_].current = false;
        stages_[currentStage_].completed = true;
        stages_[currentStage_].progress = 1.0f;
        
        currentStage_ = stage;
        stages_[currentStage_].current = true;
        stages_[currentStage_].progress = 0.0f;
        
                switch (stage) {
            case Stage::LEXING: overallProgress_ = 0.0f; break;
            case Stage::PARSING: overallProgress_ = 0.2f; break;
            case Stage::CODE_GENERATION: overallProgress_ = 0.4f; break;
            case Stage::OPTIMIZATION: overallProgress_ = 0.7f; break;
            case Stage::LINKING: overallProgress_ = 0.85f; break;
            case Stage::COMPLETE: overallProgress_ = 1.0f; break;
        }
    }
}

void CompilationProgress::setStage(Stage stage, const std::string& detail) {
    setStage(stage);
    stages_[stage].name = detail;
}

void CompilationProgress::setProgress(float progress) {
    stages_[currentStage_].progress = std::clamp(progress, 0.0f, 1.0f);
}

void CompilationProgress::completeStage(Stage stage) {
    stages_[stage].completed = true;
    stages_[stage].progress = 1.0f;
}

void CompilationProgress::setError(const std::string& errorMessage) {
    errorMessage_ = errorMessage;
    running_ = false;
}

void CompilationProgress::setLiveUpdates(bool enabled) {
    liveUpdates_ = enabled;
}

std::string CompilationProgress::getStageName(Stage stage) const {
    auto it = stages_.find(stage);
    return it != stages_.end() ? it->second.name : "Unknown";
}

std::string CompilationProgress::getStageIcon(Stage stage, bool completed) const {
    auto it = stages_.find(stage);
    if (it == stages_.end()) return "•";
    
    if (completed) {
        return "✓";
    } else if (it->second.current && enableAnimation_) {
        return spinnerFrames_[spinnerFrame_ % spinnerFrames_.size()];
    } else {
        return it->second.icon;
    }
}

std::string CompilationProgress::getElapsedTime() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_);
    auto seconds = duration.count() / 1000.0;
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << seconds << "s";
    return oss.str();
}

Element CompilationProgress::createProgressBar(float progress) {
    int barWidth = 40;
    int filled = static_cast<int>(progress * barWidth);
    
    std::string bar;
    for (int i = 0; i < barWidth; ++i) {
        if (i < filled) {
            bar += "█";
        } else {
            bar += "░";
        }
    }
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(0) << (progress * 100) << "%";
    
    return hbox({
        text(bar) | color(Color::Cyan),
        text(" " + oss.str()) | bold
    });
}

Element CompilationProgress::createStageList() {
    Elements stageElements;
    
    std::vector<Stage> orderedStages = {
        Stage::LEXING,
        Stage::PARSING,
        Stage::CODE_GENERATION,
        Stage::OPTIMIZATION,
        Stage::LINKING
    };
    
    for (auto stage : orderedStages) {
        auto& info = stages_[stage];
        
        Elements stageRow;
        
        // Icon
        std::string icon = getStageIcon(stage, info.completed);
        Color iconColor = info.completed ? Color::Green : 
                         (info.current ? Color::Cyan : Color::GrayDark);
        stageRow.push_back(text(icon + " ") | color(iconColor));
        
        // Stage name
        Color textColor = info.current ? Color::White : Color::GrayDark;
        stageRow.push_back(text(info.name) | color(textColor));
        
                if (info.current && info.progress > 0.0f) {
            stageRow.push_back(text(" "));
            stageRow.push_back(createProgressBar(info.progress));
        }
        
        stageElements.push_back(hbox(std::move(stageRow)));
    }
    
    return vbox(std::move(stageElements));
}

Element CompilationProgress::createOverallDisplay(bool showProgressBar) {
    Elements elements;
    
    // Title
    elements.push_back(
        hbox({
            text("› Compiling ") | bold,
            text(filename_) | color(Color::Cyan)
        })
    );
    
    elements.push_back(text("")); // Empty line
    
    // Stage list
    elements.push_back(createStageList());
    
    elements.push_back(text("")); // Empty line

    // Overall progress
    if (showProgressBar) {
        elements.push_back(
            hbox({
                text("Overall: "),
                createProgressBar(overallProgress_)
            })
        );
    }
    
    // Elapsed time
    elements.push_back(
        text("Time: " + getElapsedTime()) | color(Color::GrayLight)
    );
    
    // Error message if present
    if (!errorMessage_.empty()) {
        elements.push_back(text("")); // Empty line
        elements.push_back(
            text("✗ Error: " + errorMessage_) | color(Color::Red) | bold
        );
    }
    
    return vbox(std::move(elements));
}

void CompilationProgress::renderLoop() {
    const int frameDelay = 100; // milliseconds
    int lastLineCount = 0;
    
    while (running_) {
                if (lastLineCount > 0) {
                        std::cout << "\033[" << lastLineCount << "A";
            std::cout << "\033[J";         }
        
        // Render current state
        auto document = createOverallDisplay(true);
        auto screen = Screen::Create(Dimension::Full(), Dimension::Fit(document));
        Render(screen, document);
        
                lastLineCount = screen.dimy();
        
        screen.Print();
        std::cout << std::flush;
        
        // Update spinner frame
        spinnerFrame_++;
        
        // Sleep
        std::this_thread::sleep_for(std::chrono::milliseconds(frameDelay));
    }
    
    if (lastLineCount > 0) {
        std::cout << "\033[" << lastLineCount << "A";
        std::cout << "\033[J";
        std::cout << "\r" << std::flush;
    }

    bool showProgressBar = true;
    if (overallProgress_ >= 1.0f && errorMessage_.empty()) {
        currentStage_ = Stage::COMPLETE;
        stages_[Stage::COMPLETE].completed = true;
        showProgressBar = false; // hide bar on successful completion
    }

    auto document = createOverallDisplay(showProgressBar);
    auto screen = Screen::Create(Dimension::Full(), Dimension::Fit(document));
    Render(screen, document);
    screen.Print();
    std::cout << std::endl;
}
