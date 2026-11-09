"""Inspect a Squirrel serialized function without changing game assets."""
import argparse
from pathlib import Path
import struct

OP_NAMES = [
    "LINE", "LOAD", "LOADINT", "LOADFLOAT", "DLOAD", "TAILCALL", "CALL", "PREPCALL", "PREPCALLK",
    "GETK", "MOVE", "NEWSLOT", "DELETE", "SET", "GET", "EQ", "NE", "ARITH", "BITW", "RETURN",
    "LOADNULLS", "LOADROOT", "LOADBOOL", "DMOVE", "JMP", "JCMP", "JZ", "SETOUTER", "GETOUTER",
    "OP_1D", "OP_1E", "NEWOBJ", "APPENDARRAY", "COMPARITH", "INC", "INCL", "PINC", "PINCL",
    "OP_26", "OP_27",
    "CMP", "EXISTS", "INSTANCEOF", "AND", "OR", "NEG", "NOT", "BWNOT", "CLOSURE", "YIELD", "RESUME",
    "FOREACH", "POSTFOREACH", "CLONE", "TYPEOF", "PUSHTRAP", "POPTRAP", "THROW", "NEWSLOTA", "GETBASE", "CLOSE",
]
# Squirrel 2.2.2 sqvm.cpp: only these operands index ci->_literals.
# https://sourceforge.net/projects/squirrel/files/squirrel2/squirrel%202.2.2%20stable/
LITERAL_ARG1 = {1, 4, 8, 9}


class Reader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def take(self, size):
        result = self.data[self.pos:self.pos + size]
        if len(result) != size:
            raise ValueError(f"Truncated stream at 0x{self.pos:X}")
        self.pos += size
        return result

    def u32(self):
        return struct.unpack("<I", self.take(4))[0]

    def i32(self):
        return struct.unpack("<i", self.take(4))[0]

    def tag(self, expected=0x50415254):
        actual = self.u32()
        if actual != expected:
            raise ValueError(f"Expected tag 0x{expected:08X} at 0x{self.pos - 4:X}, got 0x{actual:08X}")

    def object(self):
        kind = self.u32()
        if kind == 0x08000010:
            length = self.u32()
            return self.take(length).decode("utf-8", "replace")
        if kind == 0x01000001:
            return None
        if kind in (0x08000001, 0x08000002, 0x05000002, 0x01000008):
            value = self.i32()
            return bool(value) if kind in (0x08000001, 0x01000008) else value
        if kind in (0x08000004, 0x05000004):
            return struct.unpack("<f", self.take(4))[0]
        if kind == 0x08000000:
            return None
        raise ValueError(f"Unsupported object 0x{kind:08X} at 0x{self.pos - 4:X}")

    def function(self, path="main", child_index=None):
        self.tag()
        source, name = self.object(), self.object()
        if child_index is not None:
            path = f"{path}.{name if name else child_index}"
        self.tag()
        counts = [self.u32() for _ in range(8)]
        nliterals, nparameters, nouters, nlocals, nlines, ndefaults, ninstructions, nchildren = counts
        self.tag()
        literals = [self.object() for _ in range(nliterals)]
        self.tag()
        parameters = [self.object() for _ in range(nparameters)]
        self.tag()
        outers = [(self.u32(), self.object(), self.object()) for _ in range(nouters)]
        self.tag()
        locals_ = [(self.object(), self.u32(), self.u32(), self.u32()) for _ in range(nlocals)]
        self.tag()
        lines = [struct.unpack("<II", self.take(8)) for _ in range(nlines)]
        self.tag()
        defaults = [self.i32() for _ in range(ndefaults)]
        self.tag()
        instructions = [struct.unpack("<iBBBB", self.take(8)) for _ in range(ninstructions)]
        self.tag()
        result = {
            "path": path, "source": source, "name": name, "literals": literals, "parameters": parameters,
            "outers": outers, "locals": locals_, "lines": lines, "defaults": defaults, "instructions": instructions,
        }
        result["children"] = [self.function(path, i) for i in range(nchildren)]
        result["trailer"] = [self.u32(), self.take(1)[0], self.take(1)[0]]
        return result


def parse(path):
    reader = Reader(Path(path).read_bytes())
    if reader.take(2) != b"\xfa\xfa" or reader.u32() != 0x53514952:
        raise ValueError("Not a Squirrel closure stream")
    if reader.u32() != 1:
        raise ValueError("Unexpected character size")
    root = reader.function()
    if reader.u32() != 0x5441494C:
        raise ValueError("Missing closure tail")
    if reader.pos != len(reader.data):
        raise ValueError(f"Unparsed data at 0x{reader.pos:X}")
    return root


def flatten(function):
    yield function
    for child in function["children"]:
        yield from flatten(child)


def disassemble(function):
    lines = [f"{function['path']} name={function['name']!r} source={function['source']} parameters={function['parameters']}"]
    literals = function["literals"]
    lines.append("literals: " + repr(literals))
    for index, (arg1, opcode, arg0, arg2, arg3) in enumerate(function["instructions"]):
        name = OP_NAMES[opcode] if opcode < len(OP_NAMES) else f"OP_{opcode:02X}"
        extra = f" ; literal={literals[arg1]!r}" if opcode in LITERAL_ARG1 and 0 <= arg1 < len(literals) else ""
        lines.append(f"{index:04d} {name:12s} {arg0:4d} {arg1:6d} {arg2:4d} {arg3:4d}{extra}")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input")
    parser.add_argument("--names", default="Update,UpdateDraw,main")
    parser.add_argument("--output")
    args = parser.parse_args()
    root = parse(args.input)
    wanted = set(args.names.split(","))
    text = "\n\n".join(disassemble(function) for function in flatten(root) if "*" in wanted or function["name"] in wanted)
    if args.output:
        Path(args.output).write_text(text + "\n", encoding="utf-8")
    else:
        print(text)


if __name__ == "__main__":
    main()
