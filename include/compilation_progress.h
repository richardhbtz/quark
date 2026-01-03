#pragma once
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>
#include <chrono>
#include <memory>
#include <atomic>
#include <thread>
#include <map>
#include <algorithm>

// FTXUI-based compilation progress indicator
class CompilationProgress {
public:
    enum class Stage {
        LEXING,
        PARSING,
        CODE_GENERATION,
        OPTIMIZATION,
        LINKING,
        COMPLETE
    };
    
    CompilationProgress(bool enableAnimation = true);
    ~CompilationProgress();
    
    // Start/stop the progress display
    void start(const std::string& filename);
    void stop();
    
    // Update current stage
    void setStage(Stage stage);
    void setStage(Stage stage, const std::string& detail);
    
    // Update progress within a stage (0.0 to 1.0)
    void setProgress(float progress);
    
    // Mark stage as complete
    void completeStage(Stage stage);
    
    // Set error state
    void setError(const std::string& errorMessage);
    
    // Enable/disable live updates
    void setLiveUpdates(bool enabled);
    
private:
    struct StageInfo {
        std::string name;
        std::string icon;
        bool completed = false;
        bool current = false;
        float progress = 0.0f;
    };
    
    bool enableAnimation_;
    bool liveUpdates_;
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> renderThread_;
    
    std::string filename_;
    std::string errorMessage_;
    Stage currentStage_;
    float overallProgress_;
    std::chrono::steady_clock::time_point startTime_;
    
    std::map<Stage, StageInfo> stages_;
    
    // Rendering
    void renderLoop();
    ftxui::Element createProgressBar(float progress);
    ftxui::Element createStageList();
    ftxui::Element createOverallDisplay(bool showProgressBar = true);
    std::string getStageName(Stage stage) const;
    std::string getStageIcon(Stage stage, bool completed) const;
    std::string getElapsedTime() const;
    
    // Spinner animation frames
    std::vector<std::string> spinnerFrames_ = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    size_t spinnerFrame_ = 0;
};
