"""Check the production DLL error boundary without building or loading C code."""
import ast
from contextlib import redirect_stderr
import ctypes
import io
from pathlib import Path
from types import SimpleNamespace
import sys

runner = Path(__file__).resolve().parents[1] / "run.py"
tree = ast.parse(runner.read_text(encoding="utf-8"), filename=str(runner))
loader = next((n for n in tree.body if isinstance(n, ast.FunctionDef)
               and n.name == "load_check_library"), None)
assert loader is not None, "production loader must explain application-control blocks"
module = ast.Module(body=[loader], type_ignores=[])
mode = 2
changes = []
setting_fails = restoring_fails = False


def get_mode():
    return mode


def set_mode(new, previous):
    global mode
    if (previous is not None and setting_fails) or (previous is None and restoring_fails):
        return 0
    if previous is not None:
        previous._obj.value = mode
    changes.append(new)
    mode = new
    return 1


api = SimpleNamespace(GetThreadErrorMode=get_mode, SetThreadErrorMode=set_mode)
api_error = OSError("injected thread-reporting API failure")
api_error.winerror = 87
namespace = {"c": SimpleNamespace(WinDLL=lambda *a, **k: api,
              c_uint32=ctypes.c_uint32, POINTER=ctypes.POINTER, c_int=ctypes.c_int,
              byref=ctypes.byref, WinError=lambda _: api_error, get_last_error=lambda: 87), "sys": sys}
exec(compile(module, str(runner), "exec"), namespace)
load = namespace["load_check_library"]
path = Path("app_tasks_init.dll")

result = object()


def loaded(_):
    assert mode == (original | 1), "only the current DLL load should suppress critical-error UI"
    return result


namespace["c"].CDLL = loaded
for original in (0, 2, 1, 3):
    mode = original
    changes.clear()
    namespace["c"].CDLL = loaded
    assert load(path) is result
    assert mode == original and changes == [original | 1, original]
    for code in (4551, 126, "interrupt"):
        failure = KeyboardInterrupt() if code == "interrupt" else OSError("injected OS loader error")
        if code != "interrupt":
            failure.winerror = code

        def fail(_):
            assert mode == (original | 1)
            raise failure

        namespace["c"].CDLL = fail
        try:
            load(path)
        except SystemExit as error:
            assert code == 4551
            assert "app_tasks_init.dll" in str(error.code)
            assert "BLOCKED" in str(error.code) and "4551" in str(error.code)
            assert "runtime checks have not started" in str(error.code)
        except (OSError, KeyboardInterrupt) as error:
            assert code != 4551 and error is failure
        else:
            raise AssertionError("OS failure must not be accepted")
        assert mode == original, "mode must be restored on every load-error path"

# Failure to set reporting mode must abort before loading an unguarded DLL.
setting_fails = True
changes.clear()
namespace["c"].CDLL = lambda _: (_ for _ in ()).throw(AssertionError("must not load"))
try:
    load(path)
except OSError as error:
    assert error is api_error and not changes
else:
    raise AssertionError("failed reporting setup was accepted")
setting_fails = False

# Restore failure is fatal after success; an existing error keeps precedence.
restoring_fails = True
original = mode = 2
namespace["c"].CDLL = loaded
try:
    load(path)
except OSError as error:
    assert error is api_error
else:
    raise AssertionError("failed restoration was accepted")
for code in (4551, 126):
    failure = OSError("injected OS loader error")
    failure.winerror = code
    namespace["c"].CDLL = lambda _: (_ for _ in ()).throw(failure)
    messages = io.StringIO()
    try:
        with redirect_stderr(messages):
            load(path)
    except SystemExit as error:
        assert code == 4551 and "BLOCKED" in str(error.code)
    except OSError as error:
        assert code == 126 and error is failure
    else:
        raise AssertionError("restoration masked the primary error")
    assert "ERROR restoring" in messages.getvalue()
print("PASS host loader diagnostic: scoped reporting/restoration, nonzero blocks and other errors")
