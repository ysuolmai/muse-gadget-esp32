"""Exercise the portal's URL decoder with actual browser form payloads."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest


class ProvisionPortalTest(unittest.TestCase):
    def test_form_decoder(self):
        source = (Path(__file__).resolve().parents[1] / "main/provision_portal.c").read_text()
        decoder = source[source.index("static bool ap_password_valid"):source.index("static esp_err_t send_html")]
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
''' + decoder + r'''
int main(void) {
    assert(ap_password_valid("12345678"));
    assert(!ap_password_valid(""));
    assert(!ap_password_valid("1234567"));
    assert(!ap_password_valid("中文密码12345678"));
    assert(!ap_password_valid("1234567\n"));
    char pass[65];
    memset(pass, 'x', 63); pass[63] = 0;
    assert(ap_password_valid(pass));
    pass[63] = 'x'; pass[64] = 0;
    assert(!ap_password_valid(pass));
    char out[65];
    bool present;
    assert(form_value("ssid=%E4%B8%AD%E6%96%87+WiFi&password=a%26b%3Dc%25%2B",
                      "ssid", out, sizeof(out), &present));
    assert(present && strcmp(out, "中文 WiFi") == 0);
    assert(form_value("ssid=x&password=a%26b%3Dc%25%2B", "password", out, sizeof(out), NULL));
    assert(strcmp(out, "a&b=c%+") == 0);
    assert(form_value("ignored&ssid=home", "ssid", out, sizeof(out), NULL));
    assert(strcmp(out, "home") == 0);
    assert(form_value("ssid=home", "token", out, sizeof(out), &present));
    assert(!present && out[0] == 0);
    assert(form_value("ap_password=a%26b%3Dc%25%2B1", "ap_password", out, sizeof(out), NULL));
    assert(strcmp(out, "a&b=c%+1") == 0 && ap_password_valid(out));
    assert(form_value("token=", "token", out, sizeof(out), &present));
    assert(present && out[0] == 0);
    assert(form_value("ssid=1234", "ssid", out, 5, NULL));
    assert(!form_value("ssid=12345", "ssid", out, 5, NULL));
    assert(form_value("ssid=%41%42", "ssid", out, 3, NULL));
    assert(!form_value("ssid=%41%42", "ssid", out, 2, NULL));
    const char *bad[] = {"ssid=%", "ssid=%2", "ssid=%GG", "ssid=%00", "ssid=%0a", "ssid=%7f"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        assert(!form_value(bad[i], "ssid", out, sizeof(out), NULL));
    assert(!form_value("ssid=x", "ssid", out, 0, NULL));
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            src = Path(temp) / "portal.c"
            binary = Path(temp) / "portal"
            src.write_text(harness)
            subprocess.run([*shlex.split(os.environ.get("CC", "cc")),
                            "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(src), "-o", str(binary)], check=True, capture_output=True)
            subprocess.run([str(binary)], check=True, capture_output=True)
