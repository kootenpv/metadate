import pytest
from metadate import parse_date as _original_parse_date


def pytest_addoption(parser):
    parser.addoption(
        "--c-scanner",
        action="store_true",
        default=False,
        help="Run tests using the CScanner (C extension) instead of the old Scanner",
    )


@pytest.fixture(autouse=True)
def _patch_scanner(request, monkeypatch):
    if request.config.getoption("--c-scanner"):
        def patched(*args, **kwargs):
            kwargs["use_c_scanner"] = True
            return _original_parse_date(*args, **kwargs)
        monkeypatch.setattr("metadate.parse_date", patched)
