"""Check diagnostic names and operand annotations against an original bulletBoss CV4."""
import argparse
from decompile_cv4 import disassemble, flatten, parse


def check_paths(function):
    for index, child in enumerate(function["children"]):
        suffix = child["name"] if child["name"] else index
        assert child["path"] == f"{function['path']}.{suffix}"
        check_paths(child)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", help="Original decoded data/actor/bulletBoss.cv4")
    args = parser.parse_args()
    root = parse(args.input)
    check_paths(root)
    functions = list(flatten(root))
    for boss in range(1, 6):
        name = f"Boss{boss}_ClearBack"
        matches = [f for f in functions if f["name"] == name]
        assert len(matches) == 1
        function = matches[0]
        assert function["path"].endswith("." + name)
        assert "SetMotion" in function["literals"]
        motions = [arg1 for arg1, op, *_ in function["instructions"] if op == 2 and 2999 <= arg1 <= 6999]
        assert motions == [1999 + 1000 * boss], (name, motions)
        text = disassemble(function)
        assert f"name={name!r}" in text.splitlines()[0]
        assert any("LOAD " in line and "; literal=" in line for line in text.splitlines())
        for line in text.splitlines()[2:]:
            if line.split()[1] in ("SET", "NEWSLOT", "DELETE", "NEWSLOTA"):
                assert "; literal=" not in line
    print("PASS: serialized closure names/anonymous indices, five ClearBack motions 2999..6999, LOAD literals and register-only SET/NEWSLOT annotations")


if __name__ == "__main__":
    main()
