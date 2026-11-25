#include "tui/appliance_tui.hpp"

#include <cstdlib>
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
    // Core 3 (0x08) on 4-core RK3326 handhelds; Core 5 (0x20) on 6-core x86_64 desktop
    CPU_SET(is_handheld ? 3 : 5, &set);
    ::sched_setaffinity(0, sizeof(set), &set);
}

} // namespace

int main() {
    ax::tui::SystemTelemetry tel = ax::tui::ProbeTelemetry();
    PinUiToHousekeepingCore(tel.is_handheld);

    const std::vector<ax::tui::MenuItem> handheld_items = {
        {"FLAC_AX Audiophile",   "Bit-Perfect S32_LE / DoP & .MOD",    "/usr/sbin/ax-audio-route prompt && /usr/bin/flac_ax"},
        {"FinalBurn Neo (FBNeo)","Arcade, Neo Geo & CPS1-3 (640x480)", "taskset 0x06 /usr/bin/retroarch -c /etc/retroarch-2go.cfg -L /usr/lib/libretro/fbneo_libretro.so"},
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
    size_t selected = 0;

    while (true) {
        tel = ax::tui::ProbeTelemetry();
        ax::tui::RenderScreen(items, selected, tel);

        char ch = 0;
        if (::read(STDIN_FILENO, &ch, 1) <= 0) {
            continue;
        }

        if (ch == '\033') {
            char seq = 0;
            if (::read(STDIN_FILENO, &seq, 1) == 1 && seq == '[') {
                if (::read(STDIN_FILENO, &seq, 1) == 1) {
                    if (seq == 'A') { // Up arrow
                        selected = (selected == 0) ? (items.size() - 1) : (selected - 1);
                    } else if (seq == 'B') { // Down arrow
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
        } else if (ch == 'r' || ch == 'R') {
            continue;
        } else if (ch == 'q' || ch == 'Q') {
            break;
        } else if (ch == '\r' || ch == '\n') {
            std::printf("%s\033[2J\033[H", ax::tui::kReset);
            std::fflush(stdout);
            int rc = std::system(items[selected].command);
            (void)rc;
        }
    }

    return 0;
}
```[cite: 16]

### Verification Steps

<Steps>
  <Step subtitle="1 min" title="Commit Updated appliance_tui_main.cpp in Browser">
    Open `src/appliance_tui_main.cpp` in `galaxywarriordad/FLAC_AX`, replace its contents with the code above, and click **Commit changes...** to `main`.
    *Verification:* The file view on `main` shows `#include "tui/appliance_tui.hpp"` on line 1.
  </Step>
  <Step subtitle="1 min" title="Check Configure & Build in GitHub Actions">
    Open the newly triggered workflow run on `main` and expand **Configure & Build (Release, Clang)** to confirm `ax_appliance_tui` compiles past line 1.
    *Verification:* `CMakeFiles/ax_appliance_tui.dir/src/appliance_tui_main.cpp.o` builds without `appliance_tui.hpp: No such file or directory`.
  </Step>
</Steps>

For a quick refresher on CMake target include directories and out-of-tree header paths, watch [CMake Tutorial - Target Include Directories](https://www.youtube.com/watch?v=r1b_6w1z7lU).

<ElicitationsGroup message="What happens once appliance_tui_main.cpp is committed?">
  <Elicitation label="ax_appliance_tui compiles now" query="ax_appliance_tui compiled cleanly—what is the next error or step?"/>
  <Elicitation label="Another target fails" query="The build moved past ax_appliance_tui and failed on another target. Let me share the log."/>
  <Elicitation label="Wire CLAP headers next" query="Let's wire clap/clap.h for ax_clap_host next."/>
</ElicitationsGroup>
