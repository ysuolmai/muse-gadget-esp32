"""Run the production startup gesture with deterministic button edges/time."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest


class SetupButtonTest(unittest.TestCase):
    def test_startup_and_runtime_gestures(self):
        source = (Path(__file__).resolve().parents[1] /
                  'components/muse/muse_input.c').read_text()
        gesture = source[source.index('static unsigned setup_boot_talk('):
                         source.index('static void setup_button_hold(')]
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(x) (x)
#define SETUP_BOOT_WINDOW_MS 10000
#define MUSE_BTN_TALK_PRESS 1u
#define MUSE_BTN_TALK_RELEASE 2u
#define MUSE_SETUP_BUTTON_TALK 1
static struct { int setup_button; } board = {1}, *muse_board = &board;
static bool s_setup_hold_triggered;
static int portals;
static void provision_portal_start_async(void) { portals++; }
''' + gesture + r'''
int main(int argc, char **argv) {
    assert(argc == 2);
    int scenario = atoi(argv[1]);
    if (scenario == 0) {
        // Startup hold starts before deadline and completes after it.
        assert(setup_boot_talk(1, 9000, 0) == 0);
        assert(setup_boot_talk(0, 13999, 0) == 0 && portals == 0);
        assert(setup_boot_talk(0, 14000, 0) == 0 && portals == 1);
        assert(setup_boot_talk(0, 15000, 0) == 0 && portals == 1);
        assert(setup_boot_talk(2, 16000, 0) == 0);
        assert(setup_boot_talk(1, 17000, 0) == 1);
    } else if (scenario == 1) {
        // Ordinary runtime holds, even much longer than 5 s, only reach Talk.
        assert(setup_boot_talk(1, 10000, 0) == 1);
        assert(setup_boot_talk(0, 30000, 0) == 0);
        assert(setup_boot_talk(2, 31000, 0) == 2 && portals == 0);
    } else if (scenario == 2) {
        // Short startup presses do not combine into a 5 s hold.
        assert(setup_boot_talk(1, 1000, 0) == 0);
        assert(setup_boot_talk(2, 4000, 0) == 0);
        assert(setup_boot_talk(1, 4500, 0) == 0);
        assert(setup_boot_talk(2, 7500, 0) == 0 && portals == 0);
        assert(setup_boot_talk(1, 10000, 0) == 1);
    } else if (scenario == 3) {
        // Box0's independent setup key never intercepts Talk.
        board.setup_button = 2;
        assert(setup_boot_talk(1, 0, 0) == 1);
        assert(setup_boot_talk(2, 6000, 0) == 2 && portals == 0);
    } else {
        // Tick rollover and unrelated key bits survive filtering.
        TickType_t start = UINT32_MAX - 2000;
        assert(setup_boot_talk(1 | 4, start + 1000, start) == 4);
        assert(setup_boot_talk(0, start + 6000, start) == 0 && portals == 1);
        assert(setup_boot_talk(2 | 4, start + 7000, start) == 4);
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            src, binary = Path(temp) / 'gesture.c', Path(temp) / 'gesture'
            src.write_text(harness)
            subprocess.run([*shlex.split(os.environ.get('CC', 'cc')),
                            '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(src), '-o', str(binary)], check=True, capture_output=True)
            for scenario in range(5):
                with self.subTest(scenario=scenario):
                    subprocess.run([str(binary), str(scenario)], check=True,
                                   capture_output=True)
