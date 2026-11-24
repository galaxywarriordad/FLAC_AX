#include "tui/appliance_tui.hpp"
#include "video/ax_encode_2go.hpp"

#include <csignal>
#include <iostream>
#include <string>
#include <vector>

namespace {

volatile std::sig_atomic_t g_stop_requested = 0;

void handle_signal(int) noexcept {
	g_stop_requested = 1;
}

void print_usage(const char* prog) {
	std::cout << "Usage: " << prog << " [--encode <input_video> [output_video]]\n"
	          << "  Without arguments: launches the interactive Core 5 Espresso Appliance TUI.\n"
	          << "  --encode: runs ax_encode_2go on Core 5 (640x480 30fps H.264 + 48kHz stereo AAC).\n";
}

} // namespace

int main(int argc, char** argv) {
	std::signal(SIGINT, handle_signal);
	std::signal(SIGTERM, handle_signal);

	// Pin the main TUI thread to Core 5 (0x20) before entering the loop.
	(void)flac_ax::video::pin_current_thread_to_core5();

	if (argc >= 2) {
		const std::string arg1 = argv[1];
		if (arg1 == "-h" || arg1 == "--help") {
			print_usage(argv[0]);
			return 0;
		}
		if (arg1 == "--encode") {
			if (argc < 3) {
				std::cerr << "[appliance_tui_main] Missing <input_video> after --encode\n";
				return 1;
			}
			std::vector<std::string> encode_args;
			encode_args.reserve(static_cast<size_t>(argc - 2));
			for (int i = 2; i < argc; ++i) {
				encode_args.emplace_back(argv[i]);
			}
			return flac_ax::video::run_encode_2go_process(encode_args);
		}
		std::cerr << "[appliance_tui_main] Unknown option '" << arg1 << "'\n";
		print_usage(argv[0]);
		return 1;
	}

	if (g_stop_requested != 0) {
		return 0;
	}

	return flac_ax::tui::run_appliance_tui_loop();
}
