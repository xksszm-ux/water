"""Run production health/state clock regression checks; no boards or packages.

The former review reproducer now expects the repaired protection boundary.
Reuses the runner's compiler helper, CRT library and DLL error boundary.
Run software_checks/run.py first to generate build/crt.lib.
"""
import ast
import ctypes as c
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "software_checks/build"
runner = ROOT / "software_checks/run.py"
tree = ast.parse(runner.read_text(encoding="utf-8"), filename=str(runner))
names = {"find_tool", "run", "load_check_library", "build_boundary_checks",
         "CLANG", "LINK", "SERVICE_DIRS"}
nodes = [node for node in tree.body if
         (isinstance(node, ast.FunctionDef) and node.name in names) or
         (isinstance(node, ast.Assign) and any(
             isinstance(target, ast.Name) and target.id in names for target in node.targets))]
exec(compile(ast.Module(body=nodes, type_ignores=[]), str(runner), "exec"))
if not (BUILD / "crt.lib").is_file():
    raise SystemExit("Run software_checks/run.py first to build the CRT import library.")
checks = (
    (build_boundary_checks("clock_app_tasks", ["software_checks/startup/app_tasks_init_checks.c"],
        "software_checks/startup_stubs", ["shared", "stm32/Application/Tasks",
        *SERVICE_DIRS, "stm32/Drivers/Motor"], ["AppTasksInitChecks_Run"]), "AppTasksInitChecks_Run"),
    (build_boundary_checks("clock_repro", ["software_checks/logic/clock_repro_checks.c"],
        "software_checks/stubs", ["stm32/Application/Services/State"],
        ["ClockRepro_Run"]), "ClockRepro_Run"),
)
loaded = [(load_check_library(path), name) for path, name in checks]
for dll, name in loaded:
    check = getattr(dll, name)
    check.restype = c.c_int
    line = check()
    assert line == 0, f"{name}: source line {line}: protected clock regression failed"
    print(f"PASS {name}: protected clocks, injected preemption ordering and deadline/wrap boundaries")
print("Sequential boundary only; real task preemption and hardware were not executed.")
