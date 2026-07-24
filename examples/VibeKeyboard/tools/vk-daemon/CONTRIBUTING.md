# Contributing

Use English for code, commit messages, and public API names.

Before submitting changes, run:

```bash
uv run pytest
uv run ruff check .
uv run mypy src
```

Keep protocol changes covered by golden-vector tests. Keep transport changes covered by
async unit tests, including disconnect and malformed-frame behavior.
