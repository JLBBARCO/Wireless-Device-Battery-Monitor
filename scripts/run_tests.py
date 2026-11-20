import subprocess
import sys
import os

tests = [
    ("device_aliases_test", ["tests/device_aliases_test.cpp"]),
    ("alert_rules_test", ["tests/alert_rules_test.cpp"]),
    ("history_model_test", ["tests/history_model_test.cpp"]),
    ("runtime_estimate_test", ["tests/runtime_estimate_test.cpp"]),
    ("battery_intelligence_test", ["tests/battery_intelligence_test.cpp", "src/battery_intelligence.cpp", "src/i18n.cpp"]),
    ("multi_battery_test", ["tests/multi_battery_test.cpp"]),
    ("notifications_expansion_test", ["tests/notifications_expansion_test.cpp"]),
    ("dashboard_model_test", ["tests/dashboard_model_test.cpp", "src/battery_intelligence.cpp", "src/i18n.cpp"]),
    ("brands_test", ["tests/brands_test.cpp", "src/brands.cpp"]),
    ("labels_test", ["tests/labels_test.cpp", "src/labels.cpp"]),
    ("aula_mouse_driver_test", ["tests/aula_mouse_driver_test.cpp", "src/drivers/aula_mouse.cpp"]),
    ("darkrim_driver_test", ["tests/darkrim_driver_test.cpp", "src/drivers/darkrim.cpp"]),
    ("nuphy_driver_test", ["tests/nuphy_driver_test.cpp", "src/drivers/nuphy.cpp"]),
    ("niceboy_driver_test", ["tests/niceboy_driver_test.cpp", "src/drivers/niceboy.cpp"]),
    ("vaxee_driver_test", ["tests/vaxee_driver_test.cpp", "src/drivers/vaxee.cpp"]),
    ("finalmouse_driver_test", ["tests/finalmouse_driver_test.cpp", "src/drivers/finalmouse.cpp"]),
]

os.makedirs("build", exist_ok=True)
all_ok = True

for name, sources in tests:
    exe = f"build/{name}.exe"
    compile_cmd = ["g++", "-std=c++17", "-O2", "-Wall"] + sources + ["-o", exe]
    res = subprocess.run(compile_cmd)
    if res.returncode != 0:
        print(f"FAILED to compile {name}")
        all_ok = False
        continue
    run_res = subprocess.run([exe])
    if run_res.returncode != 0:
        print(f"FAILED test {name}")
        all_ok = False
    else:
        print(f"PASSED {name}")

if not all_ok:
    sys.exit(1)
print("\n=== ALL TESTS PASSED! ===")
