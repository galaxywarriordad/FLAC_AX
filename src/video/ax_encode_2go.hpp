#pragma once

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#if defined(FLAC_AX_HAS_AX_SDK) && FLAC_AX_HAS_AX_SDK
#include "ax/topology.hpp"
#endif

namespace flac_ax::video {

inline constexpr int kCore5HousekeepingCpu = 5;

inline bool pin_current_thread_to_core5() noexcept {
#if defined(FLAC_AX_HAS_AX_SDK) && FLAC_AX_HAS_AX_SDK
	return ax::topology::pin_to_housekeeping_core5();
#else
	if (kCore5HousekeepingCpu < 0 || kCore5HousekeepingCpu >= CPU_SETSIZE) {
		return false;
	}
	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(kCore5HousekeepingCpu, &mask);
	return sched_setaffinity(0, sizeof(mask), &mask) == 0;
#endif
}

inline std::string resolve_encode_script_path() {
	const char* env_script = std::getenv("AX_ENCODE_2GO_SCRIPT");
	if (env_script && *env_script != '\0') {
		return env_script;
	}
	if (std::filesystem::exists("./ax-encode-2go")) {
		return "./ax-encode-2go";
	}
	if (std::filesystem::exists("/usr/bin/ax-encode-2go")) {
		return "/usr/bin/ax-encode-2go";
	}
	return "ax-encode-2go";
}

inline int run_encode_2go_process(const std::vector<std::string>& args) {
	if (!pin_current_thread_to_core5()) {
		std::cerr << "[ax_encode_2go] Warning: could not lock affinity to Core "
		          << kCore5HousekeepingCpu << '\n';
	}

	const std::string script = resolve_encode_script_path();
	std::vector<char*> argv;
	argv.reserve(args.size() + 2);
	argv.push_back(const_cast<char*>(script.c_str()));
	for (const auto& arg : args) {
		argv.push_back(const_cast<char*>(arg.c_str()));
	}
	argv.push_back(nullptr);

	const pid_t pid = fork();
	if (pid < 0) {
		std::cerr << "[ax_encode_2go] fork() failed: " << std::strerror(errno) << '\n';
		return 1;
	}

	if (pid == 0) {
		(void)pin_current_thread_to_core5();
		execvp(script.c_str(), argv.data());
		std::cerr << "[ax_encode_2go] execvp('" << script
		          << "') failed: " << std::strerror(errno) << '\n';
		_exit(127);
	}

	int status = 0;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR) {
			return 1;
		}
	}

	if (WIFEXITED(status)) {
		return WEXITSTATUS(status);
	}
	if (WIFSIGNALED(status)) {
		return 128 + WTERMSIG(status);
	}
	return 1;
}

} // namespace flac_ax::video
