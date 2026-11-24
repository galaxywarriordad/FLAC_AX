#include "video/ax_encode_2go.hpp"

#include <string>
#include <vector>

int main(int argc, char** argv) {
	std::vector<std::string> args;
	args.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0);
	for (int i = 1; i < argc; ++i) {
		args.emplace_back(argv[i]);
	}
	return flac_ax::video::run_encode_2go_process(args);
}
