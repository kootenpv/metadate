import pytest
from metadate import parse_date as _original_parse_date


def pytest_addoption(parser):
    parser.addoption(
        "--new-scanner",
        action="store_true",
        default=False,
        help="Run tests using the NewScanner instead of the old Scanner",
    )


@pytest.fixture(autouse=True)
def _patch_scanner(request, monkeypatch):
    if not request.config.getoption("--new-scanner"):
        return

    def patched(*args, **kwargs):
        kwargs["use_new_scanner"] = True
        return _original_parse_date(*args, **kwargs)

    monkeypatch.setattr("metadate.parse_date", patched)
