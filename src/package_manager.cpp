#include "../include/package_manager.h"
#include "../include/cli.h"
#include "../include/compiler_api.h"

#include "../lib/toml/toml.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <cwchar>
#endif

namespace
{

	using Path = std::filesystem::path;

	struct OptimizationSettings
	{
		bool enabled = false;
		int level = 0;
		bool debugInfo = true;
		bool lto = false;
	};

	struct BuildSettings
	{
		std::string target = "executable";
		std::string outputName = "quark_project";
		bool freestanding = false;
		bool emitLLVM = false;
		bool emitAssembly = false;
		std::vector<std::string> sourceFiles;
		std::vector<std::string> libraryPaths;
		std::vector<std::string> linkLibraries;
	};

	struct ManifestContext
	{
		toml::table document;
		Path manifestPath;
		Path rootDir;

		static std::optional<ManifestContext> load(const Path &path)
		{
			std::error_code ec;
			Path absoluteManifest = std::filesystem::absolute(path, ec);
			if (ec)
			{
				g_cli.error("Failed to resolve manifest path: " + path.string());
				return std::nullopt;
			}

			ManifestContext ctx;

#if defined(TOML_EXCEPTIONS) && TOML_EXCEPTIONS
			try
			{
				ctx.document = toml::parse_file(absoluteManifest.string());
			}
			catch (const toml::parse_error &err)
			{
				g_cli.error("Failed to parse " + absoluteManifest.string() + ": " + std::string(err.description()));
				return std::nullopt;
			}
#else
			toml::parse_result result = toml::parse_file(absoluteManifest.string());
			if (!result)
			{
				g_cli.error("Failed to parse " + absoluteManifest.string() + ": " + std::string(result.error().description()));
				return std::nullopt;
			}
			ctx.document = std::move(result).table();
#endif
			ctx.manifestPath = std::move(absoluteManifest);
			ctx.rootDir = ctx.manifestPath.parent_path();
			return ctx;
		}

		bool save() const
		{
			std::ofstream out(manifestPath, std::ios::binary | std::ios::trunc);
			if (!out.is_open())
			{
				g_cli.error("Failed to open " + manifestPath.string() + " for writing");
				return false;
			}

			toml::toml_formatter formatter(document);
			out << formatter;
			if (!out.good())
			{
				g_cli.error("Failed while writing manifest: " + manifestPath.string());
				return false;
			}

			return true;
		}
	};

	static toml::table *get_table(toml::table &root, std::string_view key)
	{
		if (auto node = root.get(key))
		{
			return node->as_table();
		}
		return nullptr;
	}

	static const toml::table *get_table(const toml::table &root, std::string_view key)
	{
		if (auto node = root.get(key))
		{
			return node->as_table();
		}
		return nullptr;
	}

	static toml::table &ensure_table(toml::table &root, std::string_view key)
	{
		if (auto existing = root.get(key))
		{
			if (auto tbl = existing->as_table())
				return *tbl;
			root.erase(key);
		}

		auto [it, inserted] = root.insert_or_assign(std::string(key), toml::table{});
		(void)inserted;
		return *it->second.as_table();
	}

	static toml::array &ensure_array(toml::table &root, std::string_view key)
	{
		if (auto existing = root.get(key))
		{
			if (auto arr = existing->as_array())
				return *arr;
			root.erase(key);
		}

		auto [it, inserted] = root.insert_or_assign(std::string(key), toml::array{});
		(void)inserted;
		return *it->second.as_array();
	}

	static std::string to_lower(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
					   { return static_cast<char>(std::tolower(c)); });
		return value;
	}

	static BuildSettings read_build_settings(const toml::table &doc)
	{
		BuildSettings settings;

		if (const toml::table *build = get_table(doc, "build"))
		{
			if (auto val = build->get("target"))
			{
				if (auto s = val->value<std::string>())
					settings.target = *s;
			}

			if (auto val = build->get("output_name"))
			{
				if (auto s = val->value<std::string>())
					settings.outputName = *s;
			}

			if (auto val = build->get("freestanding"))
			{
				if (auto b = val->value<bool>())
					settings.freestanding = *b;
			}

			if (auto val = build->get("emit_llvm"))
			{
				if (auto b = val->value<bool>())
					settings.emitLLVM = *b;
			}

			if (auto val = build->get("emit_assembly"))
			{
				if (auto b = val->value<bool>())
					settings.emitAssembly = *b;
			}

			if (auto node = build->get("source_files"))
			{
				if (auto arr = node->as_array())
				{
					settings.sourceFiles.clear();
					for (const auto &item : *arr)
					{
						if (auto s = item.value<std::string>())
							settings.sourceFiles.emplace_back(*s);
					}
				}
			}

			if (auto node = build->get("library_paths"))
			{
				if (auto arr = node->as_array())
				{
					settings.libraryPaths.clear();
					for (const auto &item : *arr)
					{
						if (auto s = item.value<std::string>())
							settings.libraryPaths.emplace_back(*s);
					}
				}
			}

			if (auto node = build->get("link_libraries"))
			{
				if (auto arr = node->as_array())
				{
					settings.linkLibraries.clear();
					for (const auto &item : *arr)
					{
						if (auto s = item.value<std::string>())
							settings.linkLibraries.emplace_back(*s);
					}
				}
			}
		}

		if (settings.sourceFiles.empty())
			settings.sourceFiles.emplace_back("src/main.k");

		if (settings.outputName.empty())
			settings.outputName = "quark_project";

		return settings;
	}

	static OptimizationSettings read_optimization_profile(const toml::table &doc, std::string profile)
	{
		OptimizationSettings settings;
		std::string lowered = to_lower(profile);

		// Defaults
		if (lowered == "release")
		{
			settings.enabled = true;
			settings.level = 3;
			settings.debugInfo = false;
			settings.lto = true;
		}
		else
		{
			settings.enabled = false;
			settings.level = 0;
			settings.debugInfo = true;
			settings.lto = false;
		}

		if (const toml::table *profileRoot = get_table(doc, "profile"))
		{
			if (const toml::table *profileNode = get_table(*profileRoot, lowered))
			{
				if (const toml::table *optNode = get_table(*profileNode, "optimization"))
				{
					if (auto val = optNode->get("enabled"))
					{
						if (auto b = val->value<bool>())
							settings.enabled = *b;
					}

					if (auto val = optNode->get("level"))
					{
						if (auto i = val->value<int64_t>())
							settings.level = static_cast<int>(*i);
					}

					if (auto val = optNode->get("debug_info"))
					{
						if (auto b = val->value<bool>())
							settings.debugInfo = *b;
					}

					if (auto val = optNode->get("lto"))
					{
						if (auto b = val->value<bool>())
							settings.lto = *b;
					}
				}
			}
		}

		return settings;
	}

	static std::unordered_map<std::string, std::string> collect_dependencies(const toml::table &doc, std::string_view key)
	{
		std::unordered_map<std::string, std::string> deps;
		if (const toml::table *table = get_table(doc, key))
		{
			for (const auto &[depKey, node] : *table)
			{
				if (auto value = node.value<std::string>())
					deps.emplace(depKey.str(), *value);
			}
		}
		return deps;
	}

	static std::string current_platform_executable(const std::string &baseName)
	{
#ifdef _WIN32
		if (baseName.size() > 4 && baseName.substr(baseName.size() - 4) == ".exe")
			return baseName;
		return baseName + ".exe";
#else
		return baseName;
#endif
	}

	static std::vector<std::string> normalize_paths(const std::vector<std::string> &input, const Path &base)
	{
		std::vector<std::string> normalized;
		std::unordered_map<std::string, bool> seen;
		for (const auto &entry : input)
		{
			if (entry.empty())
				continue;

			Path candidate(entry);
			if (candidate.is_relative())
				candidate = (base / candidate);

			candidate = candidate.lexically_normal();
			std::string str = candidate.string();
			if (!seen[str])
			{
				normalized.emplace_back(str);
				seen[str] = true;
			}
		}
		return normalized;
	}

	static int run_executable(const Path &executable)
	{
		std::error_code ec;
		if (!std::filesystem::exists(executable, ec))
		{
			g_cli.error("Cannot run missing executable: " + executable.string());
			return 1;
		}

#ifdef _WIN32
		std::wstring command = L"\"" + executable.wstring() + L"\"";
		int code = _wsystem(command.c_str());
#else
		std::string command = "\"" + executable.string() + "\"";
		int code = std::system(command.c_str());
#endif

		if (code == -1)
		{
			g_cli.error("Failed to launch executable: " + executable.string());
			return 1;
		}

		if (code != 0)
		{
			g_cli.warning("Process exited with code " + std::to_string(code));
		}

		return (code == 0) ? 0 : code;
	}

	static bool ensure_directory(const Path &dir)
	{
		std::error_code ec;
		if (std::filesystem::exists(dir, ec))
			return true;

		if (std::filesystem::create_directories(dir, ec))
			return true;

		if (ec)
		{
			g_cli.error("Failed to create directory " + dir.string() + ": " + ec.message());
		}
		return false;
	}

	static bool write_file_if_missing(const Path &path, std::string_view contents, bool force)
	{
		std::error_code ec;
		if (std::filesystem::exists(path, ec) && !force)
		{
			g_cli.warning("File already exists: " + path.string());
			return false;
		}

		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out.is_open())
		{
			g_cli.error("Failed to create file: " + path.string());
			return false;
		}
		out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
		if (!out.good())
		{
			g_cli.error("Failed to write file: " + path.string());
			return false;
		}
		return true;
	}

	static toml::table create_default_manifest(const std::string &projectName)
	{
		toml::table doc;

		toml::table package;
		package.insert_or_assign("name", projectName);
		package.insert_or_assign("version", std::string("0.1.0"));
		package.insert_or_assign("description", std::string("A Quark project"));
		package.insert_or_assign("license", std::string("MIT"));
		package.insert_or_assign("authors", toml::array{});
		package.insert_or_assign("repository", std::string(""));
		package.insert_or_assign("homepage", std::string(""));
		doc.insert_or_assign("package", package);

		toml::table build;
		build.insert_or_assign("target", std::string("executable"));
		build.insert_or_assign("output_name", projectName);
		build.insert_or_assign("freestanding", false);
		build.insert_or_assign("emit_llvm", false);
		build.insert_or_assign("emit_assembly", false);

		toml::array sourceFiles;
		sourceFiles.push_back(std::string("src/main.k"));
		build.insert_or_assign("source_files", sourceFiles);

		build.insert_or_assign("library_paths", toml::array{});
		build.insert_or_assign("link_libraries", toml::array{});

		doc.insert_or_assign("build", build);

		doc.insert_or_assign("dependencies", toml::table{});

		toml::table profile;

		toml::table dev;
		toml::table devOpt;
		devOpt.insert_or_assign("enabled", false);
		devOpt.insert_or_assign("level", 0);
		devOpt.insert_or_assign("debug_info", true);
		devOpt.insert_or_assign("lto", false);
		dev.insert_or_assign("optimization", devOpt);
		profile.insert_or_assign("dev", dev);

		toml::table release;
		toml::table releaseOpt;
		releaseOpt.insert_or_assign("enabled", true);
		releaseOpt.insert_or_assign("level", 3);
		releaseOpt.insert_or_assign("debug_info", false);
		releaseOpt.insert_or_assign("lto", true);
		release.insert_or_assign("optimization", releaseOpt);
		profile.insert_or_assign("release", release);

		doc.insert_or_assign("profile", profile);

		return doc;
	}

	static std::string detect_project_name(const Path &targetDir)
	{
		Path normalized = targetDir;
		if (normalized.empty())
			return "quark_project";
		Path filename = normalized.filename();
		if (filename.empty())
			filename = normalized.lexically_normal().filename();
		std::string name = filename.string();
		if (name.empty())
			name = "quark_project";
		return name;
	}

	static bool save_manifest(const toml::table &doc, const Path &path)
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out.is_open())
		{
			g_cli.error("Failed to open " + path.string() + " for writing");
			return false;
		}
		toml::toml_formatter formatter(doc);
		out << formatter;
		if (!out.good())
		{
			g_cli.error("Failed while writing manifest: " + path.string());
			return false;
		}
		return true;
	}

	struct BuildResult
	{
		int exitCode = 0;
		Path outputPath;
	};

	static BuildResult perform_build(ManifestContext &manifest,
									 const Path &exeDir,
										 const std::string &profileName,
										 bool useCache,
										 bool clearCache,
										 const std::string &cacheDir)
	{
		BuildResult result;

		BuildSettings build = read_build_settings(manifest.document);
		OptimizationSettings opt = read_optimization_profile(manifest.document, profileName);

		if (build.sourceFiles.empty())
		{
			g_cli.error("No source files specified in build.source_files");
			result.exitCode = 1;
			return result;
		}

		if (build.target != "executable")
		{
			g_cli.warning("Only 'executable' build targets are currently supported. Requested target: " + build.target);
		}

		if (build.sourceFiles.size() > 1)
		{
			g_cli.warning("Multiple source files are listed; only the first entry will be used for compilation.");
		}

		Path inputPath = manifest.rootDir / Path(build.sourceFiles.front());
		std::error_code ec;
		inputPath = inputPath.lexically_normal();
		if (!std::filesystem::exists(inputPath, ec))
		{
			g_cli.error("Source file not found: " + inputPath.string());
			result.exitCode = 1;
			return result;
		}

		Path buildDir = manifest.rootDir / "build";
		if (!ensure_directory(buildDir))
		{
			result.exitCode = 1;
			return result;
		}

		std::string outputBase = current_platform_executable(build.outputName.empty() ? "quark_project" : build.outputName);
		Path outputPath = buildDir / outputBase;
		std::string outputPathStr = outputPath.string();
		result.outputPath = outputPath;

		QuarkCompilerHandle *compiler = quark_compiler_create();
		if (!compiler)
		{
			g_cli.error("Failed to initialize the Quark compiler library");
			result.exitCode = 1;
			return result;
		}

		std::string inputPathStr = inputPath.string();

		QuarkCompilerOptions options{};
		options.input_path = inputPathStr.c_str();
		options.output_path = outputPathStr.c_str();
		options.optimize = opt.enabled ? 1 : 0;
		options.optimization_level = opt.level;
		options.freestanding = build.freestanding ? 1 : 0;
		options.emit_llvm = build.emitLLVM ? 1 : 0;
		options.emit_asm = build.emitAssembly ? 1 : 0;
		options.verbosity = static_cast<int>(QUARK_VERBOSITY_NORMAL);
		options.color_output = g_cli.isColorEnabled() ? 1 : 0;
		options.use_cache = useCache ? 1 : 0;
		options.clear_cache = clearCache ? 1 : 0;
		if (!cacheDir.empty())
			options.cache_dir = cacheDir.c_str();

		std::vector<std::string> libraryPaths;
		if (!exeDir.empty())
			libraryPaths.push_back(exeDir.lexically_normal().string());

		for (const auto &entry : build.libraryPaths)
		{
			if (!entry.empty())
				libraryPaths.push_back(entry);
		}

		libraryPaths = normalize_paths(libraryPaths, manifest.rootDir);

		std::vector<const char *> libraryPathPtrs;
		libraryPathPtrs.reserve(libraryPaths.size());
		for (const auto &entry : libraryPaths)
			libraryPathPtrs.push_back(entry.c_str());
		options.library_paths = libraryPathPtrs.data();
		options.library_path_count = libraryPathPtrs.size();

		std::vector<const char *> linkLibPtrs;
		linkLibPtrs.reserve(build.linkLibraries.size());
		for (const auto &lib : build.linkLibraries)
			linkLibPtrs.push_back(lib.c_str());
		options.link_libraries = linkLibPtrs.data();
		options.link_library_count = linkLibPtrs.size();

		bool willShowProgress = (options.verbosity >= QUARK_VERBOSITY_NORMAL) && g_cli.isColorEnabled();

		if (!willShowProgress)
		{
			g_cli.startSpinner("Compiling " + inputPath.filename().string());
		}

		int status = quark_compiler_compile_file(compiler, &options);

		if (!willShowProgress)
		{
			g_cli.stopSpinner(status == QUARK_COMPILE_OK);
		}

		if (status != QUARK_COMPILE_OK)
		{
			g_cli.error("Compilation failed");
			result.exitCode = 1;
		}
		else
		{
			std::string displayPath = outputPath.string();
			try
			{
				Path currentPath = std::filesystem::current_path();
				displayPath = std::filesystem::relative(outputPath, currentPath).string();
			}
			catch (...)
			{
			}
			g_cli.success("Build finished: " + displayPath);
			result.exitCode = 0;
		}

		quark_compiler_destroy(compiler);
		return result;
	}

	static bool handle_add_dependency(ManifestContext &manifest, const std::string &name, const std::string &version)
	{
		toml::table &deps = ensure_table(manifest.document, "dependencies");
		deps.insert_or_assign(name, version);
		if (!manifest.save())
			return false;
		g_cli.success("Added " + name + " = \"" + version + "\" to dependencies");
		return true;
	}

	// Clone a git repository into the modules directory
	static bool clone_git_module(const Path &projectRoot, const std::string &repoUrl, const std::string &moduleName)
	{
		Path modulesDir = projectRoot / "modules";
		std::error_code ec;

		// Create modules directory if it doesn't exist
		if (!std::filesystem::exists(modulesDir, ec))
		{
			if (!std::filesystem::create_directories(modulesDir, ec))
			{
				g_cli.error("Failed to create modules directory: " + ec.message());
				return false;
			}
		}

		Path targetDir = modulesDir / moduleName;

		// Check if module already exists
		if (std::filesystem::exists(targetDir, ec))
		{
			g_cli.warning("Module '" + moduleName + "' already exists. Use 'quark update' to update it.");
			return false;
		}

		// Build git clone command
		std::string command = "git clone --depth 1 \"" + repoUrl + "\" \"" + targetDir.string() + "\"";

		g_cli.startSpinner("Cloning " + moduleName + " from " + repoUrl);

#ifdef _WIN32
		int result = std::system(command.c_str());
#else
		int result = std::system(command.c_str());
#endif

		g_cli.stopSpinner(result == 0);

		if (result != 0)
		{
			g_cli.error("Failed to clone repository: " + repoUrl);
			return false;
		}

		// Remove .git directory to save space (optional, can be configurable)
		Path gitDir = targetDir / ".git";
		if (std::filesystem::exists(gitDir, ec))
		{
			std::filesystem::remove_all(gitDir, ec);
		}

		g_cli.success("Installed module '" + moduleName + "' from " + repoUrl);
		return true;
	}

	// Extract module name from git URL
	static std::string extract_module_name(const std::string &repoUrl)
	{
		// Handle URLs like:
		// https://github.com/user/repo.git
		// https://github.com/user/repo
		// git@github.com:user/repo.git
		// user/repo (GitHub shorthand)

		std::string url = repoUrl;

		// Remove trailing .git
		if (url.size() > 4 && url.substr(url.size() - 4) == ".git")
		{
			url = url.substr(0, url.size() - 4);
		}

		// Find the last path component
		size_t lastSlash = url.rfind('/');
		size_t lastColon = url.rfind(':');
		size_t startPos = 0;

		if (lastSlash != std::string::npos)
		{
			startPos = lastSlash + 1;
		}
		else if (lastColon != std::string::npos)
		{
			startPos = lastColon + 1;
		}

		return url.substr(startPos);
	}

	// Expand GitHub shorthand to full URL
	static std::string expand_git_url(const std::string &input)
	{
		// If it looks like a URL, return as-is
		if (input.find("://") != std::string::npos || input.find("git@") == 0)
		{
			return input;
		}

		// Check for GitHub shorthand: user/repo
		if (input.find('/') != std::string::npos && input.find(' ') == std::string::npos)
		{
			return "https://github.com/" + input + ".git";
		}

		// Return as-is, might be a module name
		return input;
	}

	static bool handle_install_module(const Path &projectRoot, ManifestContext &manifest,
									  const std::string &input, const std::string &customName)
	{
		std::string repoUrl = expand_git_url(input);
		std::string moduleName = customName.empty() ? extract_module_name(input) : customName;

		if (moduleName.empty())
		{
			g_cli.error("Could not determine module name from: " + input);
			return false;
		}

		// Clone the repository
		if (!clone_git_module(projectRoot, repoUrl, moduleName))
		{
			return false;
		}

		// Add to dependencies in Quark.toml
		std::string key = "dependencies";
		toml::table &deps = ensure_table(manifest.document, key);

		// Store the git URL as the version
		toml::table depInfo;
		depInfo.insert_or_assign("git", repoUrl);
		deps.insert_or_assign(moduleName, depInfo);

		if (!manifest.save())
		{
			g_cli.warning("Module installed but failed to update Quark.toml");
			return true;
		}

		return true;
	}

	static bool handle_remove_dependency(ManifestContext &manifest, const std::string &name)
	{
		if (toml::table *deps = get_table(manifest.document, "dependencies"))
		{
			if (deps->erase(name))
			{
				if (!manifest.save())
					return false;
				g_cli.success("Removed " + name + " from dependencies");
				return true;
			}
		}
		g_cli.warning("No dependency named '" + name + "' found in dependencies");
		return false;
	}

	static void print_dependency_list(const ManifestContext &manifest)
	{
		auto deps = collect_dependencies(manifest.document, "dependencies");

		if (deps.empty())
		{
			g_cli.info("No dependencies declared");
			return;
		}

		g_cli.println("Dependencies:", MessageType::INFO);
		for (const auto &[name, version] : deps)
			g_cli.println("  " + name + " = " + version, MessageType::INFO);
	}

	static void print_help()
	{
		g_cli.println("Quark package manager", MessageType::INFO);
		g_cli.println("", MessageType::INFO);
		g_cli.println("Usage:", MessageType::INFO);
		g_cli.println("  quark package <command> [options]", MessageType::INFO);
		g_cli.println("  quark <command> [options]          (shorthand)", MessageType::INFO);
		g_cli.println("", MessageType::INFO);
		g_cli.println("Commands:", MessageType::INFO);
		g_cli.println("  init [path]            Initialize a new Quark project", MessageType::INFO);
		g_cli.println("  build [--release]      Build the current project", MessageType::INFO);
		g_cli.println("  run [--release]        Build and run the current project", MessageType::INFO);
		g_cli.println("  clean                  Remove build artifacts", MessageType::INFO);
		g_cli.println("  add <repo> [--as name] Install a module from git (e.g., user/repo)", MessageType::INFO);
		g_cli.println("  remove <name>          Remove a module", MessageType::INFO);
		g_cli.println("  list                   List declared dependencies", MessageType::INFO);
		g_cli.println("  help                   Show this help message", MessageType::INFO);
		g_cli.println("", MessageType::INFO);
		g_cli.println("Examples:", MessageType::INFO);
		g_cli.println("  quark add user/discord           # Install from github.com/user/discord", MessageType::INFO);
		g_cli.println("  quark add user/lib --as mylib    # Install with custom name", MessageType::INFO);
		g_cli.println("  quark add https://gitlab.com/u/r # Install from full URL", MessageType::INFO);
		g_cli.println("", MessageType::INFO);
		g_cli.println("Module imports:", MessageType::INFO);
		g_cli.println("  import json      # Standard library", MessageType::INFO);
		g_cli.println("  import discord   # Installed module", MessageType::INFO);
		g_cli.println("  import mod/sub   # Submodule", MessageType::INFO);
		g_cli.println("", MessageType::INFO);
		g_cli.println("Global options:", MessageType::INFO);
		g_cli.println("  --manifest <path>      Specify an alternate Quark.toml", MessageType::INFO);
	}

} // namespace

int run_package_manager_cli(int argc, char **argv, const std::filesystem::path &executablePath)
{
	if (argc <= 0)
	{
		print_help();
		return 0;
	}

	Path manifestPath = "Quark.toml";
	std::vector<std::string> positional;

	for (int i = 0; i < argc; ++i)
	{
		std::string_view arg{argv[i]};
		if (arg == "--manifest" || arg == "-m")
		{
			if (i + 1 >= argc)
			{
				g_cli.error("--manifest requires a path argument");
				return 1;
			}
			manifestPath = argv[++i];
			continue;
		}

		positional.emplace_back(arg);
	}

	if (positional.empty())
	{
		print_help();
		return 0;
	}

	const std::string command = positional.front();
	std::vector<std::string> commandArgs(positional.begin() + 1, positional.end());

	std::string loweredCommand = to_lower(command);

	if (loweredCommand == "help" || loweredCommand == "-h" || loweredCommand == "--help")
	{
		print_help();
		return 0;
	}

	if (loweredCommand == "init")
	{
		bool force = false;
		std::optional<Path> targetDir;
		std::optional<std::string> explicitName;

		for (size_t i = 0; i < commandArgs.size(); ++i)
		{
			std::string_view arg = commandArgs[i];
			if (arg == "--force" || arg == "-f")
			{
				force = true;
			}
			else if (arg == "--name")
			{
				if (i + 1 >= commandArgs.size())
				{
					g_cli.error("--name requires a value");
					return 1;
				}
				explicitName = commandArgs[++i];
			}
			else if (arg.rfind("--name=", 0) == 0)
			{
				explicitName = std::string(arg.substr(7));
			}
			else if (!arg.empty() && arg[0] == '-')
			{
				g_cli.error("Unknown option for init: " + std::string(arg));
				return 1;
			}
			else if (!targetDir)
			{
				targetDir = Path(arg);
			}
			else
			{
				g_cli.error("Multiple directories specified. Only one path argument is allowed.");
				return 1;
			}
		}

		Path resolvedDir;
		if (targetDir)
		{
			std::error_code ec;
			resolvedDir = std::filesystem::absolute(*targetDir, ec);
			if (ec)
			{
				g_cli.error("Failed to resolve target directory: " + targetDir->string());
				return 1;
			}
		}
		else
		{
			resolvedDir = std::filesystem::current_path();
		}

		if (!ensure_directory(resolvedDir))
			return 1;

		std::string projectName = explicitName.value_or(detect_project_name(resolvedDir));

		Path manifestFile = resolvedDir / "Quark.toml";
		std::error_code ec;
		if (std::filesystem::exists(manifestFile, ec) && !force)
		{
			g_cli.error("Quark.toml already exists at " + manifestFile.string() + "; use --force to overwrite.");
			return 1;
		}

		toml::table manifest = create_default_manifest(projectName);
		if (!save_manifest(manifest, manifestFile))
			return 1;

		Path srcDir = resolvedDir / "src";
		if (!ensure_directory(srcDir))
			return 1;

		Path mainFile = srcDir / "main.k";
		const std::string mainTemplate =
			"// Entry point generated by quark package init\n"
			"int main() {\n"
			"    print(\"Hello from Quark!\");\n"
			"    ret 0;\n"
			"}\n";
		write_file_if_missing(mainFile, mainTemplate, force);

		Path gitignore = resolvedDir / ".gitignore";
		const std::string gitignoreTemplate =
			"# Quark build artifacts\n"
			"build/\n"
			"*.exe\n";
		write_file_if_missing(gitignore, gitignoreTemplate, false);

		g_cli.success("Initialized Quark project in " + resolvedDir.string());
		return 0;
	}

	auto manifestOpt = ManifestContext::load(manifestPath);
	if (!manifestOpt)
		return 1;
	ManifestContext manifest = std::move(*manifestOpt);

	Path exeDir = executablePath.parent_path();

	if (loweredCommand == "build")
	{
		std::string profile = "dev";
		bool useCache = true;
		bool clearCache = false;
		std::string cacheDir;
		for (size_t i = 0; i < commandArgs.size(); ++i)
		{
			std::string_view arg = commandArgs[i];
			if (arg == "--release")
			{
				profile = "release";
			}
			else if (arg == "--no-cache")
			{
				useCache = false;
			}
			else if (arg == "--clear-cache")
			{
				clearCache = true;
			}
			else if (arg == "--cache-dir")
			{
				if (i + 1 >= commandArgs.size())
				{
					g_cli.error("--cache-dir requires a path");
					return 1;
				}
				cacheDir = commandArgs[++i];
			}
			else if (arg == "--profile")
			{
				if (i + 1 >= commandArgs.size())
				{
					g_cli.error("--profile requires a name");
					return 1;
				}
				profile = commandArgs[++i];
			}
			else if (!arg.empty() && arg[0] == '-')
			{
				g_cli.error("Unknown option for build: " + std::string(arg));
				return 1;
			}
			else
			{
				g_cli.error("Unexpected positional argument for build: " + std::string(arg));
				return 1;
			}
		}

		BuildResult result = perform_build(manifest, exeDir, profile, useCache, clearCache, cacheDir);
		return result.exitCode;
	}

	if (loweredCommand == "run")
	{
		std::string profile = "dev";
		bool useCache = true;
		bool clearCache = false;
		std::string cacheDir;
		for (size_t i = 0; i < commandArgs.size(); ++i)
		{
			std::string_view arg = commandArgs[i];
			if (arg == "--release")
			{
				profile = "release";
			}
			else if (arg == "--no-cache")
			{
				useCache = false;
			}
			else if (arg == "--clear-cache")
			{
				clearCache = true;
			}
			else if (arg == "--cache-dir")
			{
				if (i + 1 >= commandArgs.size())
				{
					g_cli.error("--cache-dir requires a path");
					return 1;
				}
				cacheDir = commandArgs[++i];
			}
			else if (arg == "--profile")
			{
				if (i + 1 >= commandArgs.size())
				{
					g_cli.error("--profile requires a name");
					return 1;
				}
				profile = commandArgs[++i];
			}
			else if (!arg.empty() && arg[0] == '-')
			{
				g_cli.error("Unknown option for run: " + std::string(arg));
				return 1;
			}
			else
			{
				g_cli.error("Unexpected positional argument for run: " + std::string(arg));
				return 1;
			}
		}

		BuildResult result = perform_build(manifest, exeDir, profile, useCache, clearCache, cacheDir);
		if (result.exitCode != 0)
			return result.exitCode;

		return run_executable(result.outputPath);
	}

	if (loweredCommand == "clean")
	{
		Path buildDir = manifest.rootDir / "build";
		std::error_code ec;
		if (!std::filesystem::exists(buildDir, ec))
		{
			g_cli.info("Nothing to clean");
			return 0;
		}
		std::uintmax_t removed = std::filesystem::remove_all(buildDir, ec);
		if (ec)
		{
			g_cli.error("Failed to clean build directory: " + ec.message());
			return 1;
		}
		g_cli.success("Removed " + std::to_string(removed) + " items from " + buildDir.string());
		return 0;
	}

	if (loweredCommand == "add")
	{
		std::string customName;
		std::vector<std::string> args;

		for (size_t i = 0; i < commandArgs.size(); ++i)
		{
			std::string_view arg = commandArgs[i];
			if (arg == "--as")
			{
				if (i + 1 >= commandArgs.size())
				{
					g_cli.error("--as requires a module name");
					return 1;
				}
				customName = commandArgs[++i];
			}
			else if (!arg.empty() && arg[0] == '-')
			{
				g_cli.error("Unknown option for add: " + std::string(arg));
				return 1;
			}
			else
			{
				args.emplace_back(commandArgs[i]);
			}
		}

		if (args.empty())
		{
			g_cli.error("Usage: quark add <repo> [--as name]");
			g_cli.info("Examples:");
			g_cli.info("  quark add user/repo           # GitHub shorthand");
			g_cli.info("  quark add user/repo --as lib  # Custom module name");
			return 1;
		}

		// If the input looks like a git repo, install it
		std::string input = args[0];
		bool looksLikeGit = input.find('/') != std::string::npos ||
							input.find("://") != std::string::npos ||
							input.find("git@") == 0;

		if (looksLikeGit)
		{
			return handle_install_module(manifest.rootDir, manifest, input, customName) ? 0 : 1;
		}
		else if (args.size() == 2)
		{
			// Legacy: quark add <name> <version>
			return handle_add_dependency(manifest, args[0], args[1]) ? 0 : 1;
		}
		else
		{
			g_cli.error("Invalid add command. Use: quark add <user/repo> or quark add <name> <version>");
			return 1;
		}
	}

	if (loweredCommand == "remove" || loweredCommand == "rm")
	{
		std::vector<std::string> args;
		for (size_t i = 0; i < commandArgs.size(); ++i)
		{
			std::string_view arg = commandArgs[i];
			if (!arg.empty() && arg[0] == '-')
			{
				g_cli.error("Unknown option for remove: " + std::string(arg));
				return 1;
			}
			else
				args.emplace_back(commandArgs[i]);
		}

		if (args.size() != 1)
		{
			g_cli.error("Usage: quark package remove <name>");
			return 1;
		}

		return handle_remove_dependency(manifest, args[0]) ? 0 : 1;
	}

	if (loweredCommand == "list")
	{
		print_dependency_list(manifest);
		return 0;
	}

	g_cli.error("Unknown package command: " + command);
	print_help();
	return 1;
}
