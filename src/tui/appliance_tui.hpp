#pragma once

#include "video/ax_encode_2go.hpp"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace flac_ax::tui {

struct ApplianceEntry {
	char key;
	const char* label;
	const char* detail;
	const char* binary;
};

inline constexpr std::array<ApplianceEntry, 5> kApplianceEntries{{
	{'1', "FLAC / DoP Player", "Bit-perfect ESS Sabre audio (Core 0 SCHED_FIFO 80)", "flac_ax"},
	{'2', "AX Audio/Visual Studio", "MPK Mini MK3 + AVX2 Synth + GL HUD", "ax_av_studio"},
	{'3', "DVD / HDMI Upscaler", "720p / 1080p / 2K single-stream scaler", "ax_video_upscaler"},
	{'4', "Encode 640x480 (G350)", "Core 5 QSV + GTX 980 Ti -> 640x480 60fps/30fps 48kHz AAC", "ax_encode_2go"},
	{'q', "Quit Appliance TUI", "Return to shell / init supervisor", nullptr},
}};

inline void print_appliance_banner() {
	std::cout << "\n============================================================\n"
	          << "  THE SILICON PRESERVATION SOCIETY — ESPRESSO APPLIANCE TUI\n"
	          << "  Topology: 2:3:1 | TUI & Batch Plane: Core 5 (0x20)\n"
	          << "============================================================\n";
	for (const auto& item : kApplianceEntries) {
		std::cout << "  [" << item.key << "] " << item.label
		          << " — " << item.detail << '\n';
	}
	std::cout << "Select option: " << std::flush;
}

inline int spawn_child_on_core5(const char* prog, const std::vector<std::string>& extra_args = {}) {
	(void)flac_ax::video::pin_current_thread_to_core5();

	std::vector<char*> argv;
	argv.reserve(extra_args.size() + 2);
	argv.push_back(const_cast<char*>(prog));
	for (const auto& arg : extra_args) {
		argv.push_back(const_cast<char*>(arg.c_str()));
	}
	argv.push_back(nullptr);

	const pid_t pid = fork();
	if (pid < 0) {
		std::cerr << "[appliance_tui] fork() failed: " << std::strerror(errno) << '\n';
		return 1;
	}

	if (pid == 0) {
		cpu_set_t mask;
		CPU_ZERO(&mask);
		if (flac_ax::video::kCore5HousekeepingCpu >= 0 &&
		    flac_ax::video::kCore5HousekeepingCpu < CPU_SETSIZE) {
			CPU_SET(flac_ax::video::kCore5HousekeepingCpu, &mask);
			(void)sched_setaffinity(0, sizeof(mask), &mask);
		}
		execvp(prog, argv.data());
		std::cerr << "[appliance_tui] Failed to launch '" << prog
		          << "': " << std::strerror(errno) << '\n';
		_exit(127);
	}

	int status = 0;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR) {
			return 1;
		}
	}
	return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

inline int run_encode_2go_prompt() {
	std::string input_path;
	std::string fps_choice;
	std::string output_path;

	std::cout << "Source movie path: " << std::flush;
	if (!std::getline(std::cin, input_path) || input_path.empty()) {
		std::cerr << "[appliance_tui] No source movie specified.\n";
		return 1;
	}

	std::cout << "Frame rate [60/30] (default 60): " << std::flush;
	std::getline(std::cin, fps_choice);
	const std::string fps_flag = (fps_choice == "30") ? "--30fps" : "--60fps";

	std::cout << "Output path (leave blank for default): " << std::flush;
	std::getline(std::cin, output_path);

	if (output_path.empty()) {
		return flac_ax::video::run_encode_2go_process({fps_flag, input_path});
	}
	return flac_ax::video::run_encode_2go_process({fps_flag, input_path, output_path});
}

inline int run_appliance_tui_loop() {
	(void)flac_ax::video::pin_current_thread_to_core5();

	std::string line;
	while (true) {
		print_appliance_banner();
		if (!std::getline(std::cin, line)) {
			break;
		}
		if (line.empty()) {
			continue;
		}

		const char choice = line[0];
		if (choice == 'q' || choice == 'Q') {
			return 0;
		}
		if (choice == '4') {
			const int rc = run_encode_2go_prompt();
			std::cout << "[appliance_tui] ax_encode_2go exited with code " << rc << '\n';
			continue;
		}

		const ApplianceEntry* matched = nullptr;
		for (const auto& item : kApplianceEntries) {
			if (item.key == choice && item.binary != nullptr) {
				matched = &item;
				break;
			}
		}

		if (!matched) {
			std::cout << "[appliance_tui] Unknown selection '" << choice << "'\n";
			continue;
		}

		const int rc = spawn_child_on_core5(matched->binary);
		std::cout << "[appliance_tui] " << matched->label
		          << " exited with code " << rc << '\n';
	}
	return 0;
}

} // namespace flac_ax::tui
