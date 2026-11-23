#include "appliance_tui.hpp"
#include "ax/input.hpp"

#include <cstdlib>
#include <poll.h>
#include <sched.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

namespace {

class RawTerminalGuard {
public:
    RawTerminalGuard() {
        if (::tcgetattr(STDIN_FILENO, &orig_) == 0) {
            active_ = true;
            termios raw = orig_;
            raw.c_lflag &= static_cast<tcflag_t>(~(ECHO | ICANON));
            raw.c_cc[VMIN]  = 1;
            raw.c_cc[VTIME] = 0;
            ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        }
    }
    ~RawTerminalGuard() {
        if (active_) {
            ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_);
        }
        std::printf("%s\033[2J\033[H", ax::tui::kReset);
        std::fflush(stdout);
    }

private:
    termios orig_{};
    bool active_ = false;
};

void PinUiToHousekeepingCore(bool is_handheld) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(is_handheld ? 3 : 5, &set);
    ::sched_setaffinity(0, sizeof(set), &set);
}

} // namespace

int main() {
    ax::tui::SystemTelemetry tel = ax::tui::ProbeTelemetry();
    PinUiToHousekeepingCore(tel.is_handheld);

    const std::vector<ax::tui::MenuItem> handheld_items = {
        {"FLAC_AX Audiophile",   "Bit-Perfect S32_LE / DoP & .MOD",    "/usr/sbin/ax-audio-route prompt && /usr/bin/flac_ax"},
        {"FinalBurn Neo (FBNeo)","Arcade, Neo Geo & CPS1-3 (640x480)", ". /etc/profile.d/ax-gpu.sh && /etc/init.d/S01piccolo-2go-isolate gpu-perf && taskset 0x06 /usr/bin/retroarch -c /etc/retroarch-2go.cfg -L /usr/lib/libretro/fbneo_libretro.so; /etc/init.d/S01piccolo-2go-isolate gpu-auto"},
        {"Connect / Retry DAC",  "Probe HiBy FC3 1.3V USB-C OTG",      "/usr/sbin/ax-audio-route prompt"},
        {"CPU Clock: 1.30 GHz",  "Safe Passive PLL Ceiling (1296 MHz)","/etc/init.d/S01piccolo-2go-isolate set-1.30"},
        {"CPU Clock: 1.34 GHz",  "Sweet-Spot PLL Step (+48 MHz)",      "/etc/init.d/S01piccolo-2go-isolate set-1.34"},
        {"DTB Panel Selector",   "List BATLEXP G350 / R36S Panels",    "/usr/bin/dtb-select list; printf '\\nPress Enter...'; read -r _"},
        {"Safe Shutdown",        "Sync Dual SanDisk & Power Off",      "/sbin/poweroff"}
    };

    const std::vector<ax::tui::MenuItem> desktop_items = {
        {"Console Mode",         "Batocera EmulationStation (Core 1)", "/usr/bin/emulationstation"},
        {"FLAC_AX Audiophile",   "Bit-Perfect Music & .MOD Jukebox",   "/usr/bin/flac_ax"},
        {"AV Studio Synthesizer","MPK Mini MK3 + Live MS2130S Video",  "/usr/bin/ax_av_studio"},
        {"Video Upscaler",       "Live 2K Lanczos / BFI Capture",      "/usr/bin/ax_video_upscaler"},
        {"C/C++ Workbench",      "On-Demand LXQt Desktop (< 42 MB)",   "/usr/bin/startx /usr/bin/startlxqt"},
        {"System Maintenance",   "Drop to Root Shell (Core 5)",        "/bin/sh"}
    };

    const std::vector<ax::tui::MenuItem>& items =
        tel.is_handheld ? handheld_items : desktop_items;

    RawTerminalGuard term_guard;
    ax::input::EvdevNavReader pad_nav;
    pad_nav.OpenAll();
    size_t selected = 0;

    while (true) {
        tel = ax::tui::ProbeTelemetry();
        ax::tui::RenderScreen(items, selected, tel);

        std::vector<struct pollfd> pfds;
        pfds.push_back({STDIN_FILENO, POLLIN, 0});
        for (int fd : pad_nav.fds()) {
            pfds.push_back({fd, POLLIN, 0});
        }

        if (::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), -1) <= 0) {
            continue;
        }

        bool launch = false;
        bool quit   = false;

        if (pfds[0].revents & POLLIN) {
            char ch = 0;
            if (::read(STDIN_FILENO, &ch, 1) == 1) {
                if (ch == '\033') {
                    char seq[2] = {0, 0};
                    if (::read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[') {
                        if (::read(STDIN_FILENO, &seq[1], 1) == 1) {
                            if (seq[1] == 'A') {
                                selected = (selected == 0) ? (items.size() - 1) : (selected - 1);
                            } else if (seq[1] == 'B') {
                                selected = (selected + 1) % items.size();
                            }
                        }
                    }
                } else if (ch == 'k' || ch == 'K') {
                    selected = (selected == 0) ? (items.size() - 1) : (selected - 1);
                } else if (ch == 'j' || ch == 'J') {
                    selected = (selected + 1) % items.size();
                } else if (ch >= '1' && ch <= static_cast<char>('0' + items.size())) {
                    selected = static_cast<size_t>(ch - '1');
                } else if (ch == 'q' || ch == 'Q') {
                    quit = true;
                } else if (ch == '\r' || ch == '\n') {
                    launch = true;
                }
            }
        }

        switch (pad_nav.PollNonBlocking()) {
            case ax::input::NavAction::Up:
                selected = (selected == 0) ? (items.size() - 1) : (selected - 1);
                break;
            case ax::input::NavAction::Down:
                selected = (selected + 1) % items.size();
                break;
            case ax::input::NavAction::Confirm:
                launch = true;
                break;
            default:
                break;
        }

        if (quit) break;

        if (launch) {
            pad_nav.CloseAll();
            std::printf("%s\033[2J\033[H", ax::tui::kReset);
            std::fflush(stdout);
            int rc = std::system(items[selected].command);
            (void)rc;
            pad_nav.OpenAll();
        }
    }

    return 0;
}
