#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdlib>
#include <iostream>

namespace fs = std::filesystem;

struct ExpectedOutput {
    std::vector<std::string> lines;
};

struct TestCase {
    std::string name;
    std::string file_path;
    ExpectedOutput expected;
};

static std::vector<TestCase> g_test_cases;
static fs::path g_quark_compiler;

static std::string run_command(const std::string& cmd) {
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return "";
    }
    
    std::stringstream output;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output << buffer;
    }
    pclose(pipe);
    return output.str();
}

static ExpectedOutput parse_expected_output(const std::string& file_path) {
    ExpectedOutput expected;
    std::ifstream file(file_path);
    std::string line;
    
    while (std::getline(file, line)) {
        size_t pos = line.find("EXPECT:");
        if (pos != std::string::npos) {
            std::string expect_line = line.substr(pos + 7);
            size_t start = expect_line.find_first_not_of(" \t");
            if (start != std::string::npos) {
                expect_line = expect_line.substr(start);
                size_t end = expect_line.find_last_not_of(" \t\r\n");
                if (end != std::string::npos) {
                    expect_line = expect_line.substr(0, end + 1);
                } else {
                    expect_line.clear();
                }
            } else {
                expect_line.clear();
            }
            expected.lines.push_back(expect_line);
        }
    }
    
    return expected;
}

static void discover_tests() {
    fs::path tests_dir = fs::path(__FILE__).parent_path();
    fs::path build_dir = tests_dir.parent_path() / "build";
    g_quark_compiler = build_dir / "quark";
    
    for (const auto& entry : fs::directory_iterator(tests_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".k") {
            TestCase test_case;
            test_case.name = entry.path().stem().string();
            test_case.file_path = entry.path().string();
            test_case.expected = parse_expected_output(test_case.file_path);
            g_test_cases.push_back(test_case);
        }
    }
}

class QuarkTest : public ::testing::TestWithParam<TestCase> {
protected:
};

TEST_P(QuarkTest, CompileAndRun) {
    const TestCase& test_case = GetParam();
    
    fs::path build_dir = fs::path(__FILE__).parent_path().parent_path() / "build";
    fs::path output_path = build_dir / "tests" / "bin" / test_case.name;
#ifdef _WIN32
    output_path += ".exe";
#endif
    
    fs::create_directories(output_path.parent_path());
    fs::remove(output_path);
    
    std::string compile_cmd = g_quark_compiler.string() + " -q -o " + 
                              output_path.string() + " " + test_case.file_path + 
                              " 2>&1";
    std::string compile_output = run_command(compile_cmd);
    
    if (!fs::exists(output_path)) {
        if (test_case.expected.lines.empty()) {
            GTEST_SKIP() << "No EXPECT lines, compilation failed";
        } else {
            FAIL() << "Compilation failed:\n" << compile_output;
        }
        return;
    }
    
    std::string run_cmd = output_path.string() + " 2>&1";
    std::string actual_output = run_command(run_cmd);
    
    if (test_case.expected.lines.empty()) {
        GTEST_SKIP() << "No EXPECT lines in test";
        return;
    }
    
    std::vector<std::string> actual_lines;
    std::istringstream iss(actual_output);
    std::string line;
    while (std::getline(iss, line)) {
        size_t end = line.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) {
            line = line.substr(0, end + 1);
            actual_lines.push_back(line);
        }
    }
    
    ASSERT_EQ(actual_lines.size(), test_case.expected.lines.size())
        << "Output line count mismatch. Expected " << test_case.expected.lines.size()
        << " lines, got " << actual_lines.size() << "\nActual output:\n" << actual_output;
    
    for (size_t i = 0; i < test_case.expected.lines.size(); ++i) {
        EXPECT_EQ(actual_lines[i], test_case.expected.lines[i])
            << "Line " << (i + 1) << " mismatch";
    }
}

INSTANTIATE_TEST_SUITE_P(
    QuarkCompilerTests,
    QuarkTest,
    ::testing::ValuesIn(g_test_cases),
    [](const ::testing::TestParamInfo<TestCase>& info) {
        return info.param.name;
    }
);

int main(int argc, char** argv) {
    discover_tests();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
