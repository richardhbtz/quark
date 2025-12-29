#pragma once

#include <filesystem>

// Entry point for the package manager subcommand.
//
// Arguments mirror the CLI expectations: argc/argv correspond to the tokens
// that appear after the leading "package" keyword (e.g. "quark package build").
// The executablePath should point to the resolved location of the running
// binary so the package manager can discover toolchain resources (runtime
// libraries) colocated with the executable.
//
// Returns a process-style exit code (0 on success, non-zero on failure).
int run_package_manager_cli(int argc,
							char** argv,
							const std::filesystem::path& executablePath);
