"""Check the production DLL error boundary without building or loading C code."""
import ast
from pathlib import Path
from types import SimpleNamespace

runner = Path(__file__).resolve().parents[1] / "run.py"
tree = ast.parse(runner.read_text(encoding="utf-8"), filename=str(runner))
loader = next((n for n in tree.body if isinstance(n, ast.FunctionDef)
               and n.name == "load_check_library"), None)
assert loader is not None, "production loader must explain application-control blocks"
module = ast.Module(body=[loader], type_ignores=[])
namespace = {"c": SimpleNamespace()}
exec(compile(module, str(runner), "exec"), namespace)
load = namespace["load_check_library"]
path = Path("app_tasks_init.dll")

result = object()
namespace["c"].CDLL = lambda _: result
assert load(path) is result

for code in (4551, 126):
    failure = OSError("injected OS loader error")
    failure.winerror = code

    def fail(_):
        raise failure

    namespace["c"].CDLL = fail
    try:
        load(path)
    except SystemExit as error:
        assert code == 4551
        assert "app_tasks_init.dll" in str(error.code)
        assert "BLOCKED" in str(error.code) and "4551" in str(error.code)
        assert "runtime checks have not started" in str(error.code)
    except OSError as error:
        assert code == 126 and error is failure
    else:
        raise AssertionError("OS failure must not be accepted")
print("PASS host loader diagnostic: blocked=nonzero exit, other errors preserved")
