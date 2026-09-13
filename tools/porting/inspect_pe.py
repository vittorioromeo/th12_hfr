#!/usr/bin/env python3
"""Read-only PE32/PE32+ reconnaissance. Requires pefile and capstone.

Addresses accept preferred VAs (0x140123456) or RVAs (rva:0x123456).
Xrefs are static instruction references, not a complete call/data graph.
No executable or extracted game code should be checked into this repository.
"""
import argparse
import bisect
import hashlib
import json
from pathlib import Path
import re

import capstone as cs
from capstone.x86_const import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP
import pefile


class Image:
    def __init__(self, filename):
        self.path = Path(filename)
        self.data = self.path.read_bytes()
        self.pe = pefile.PE(data=self.data)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        machine = self.pe.FILE_HEADER.Machine
        if machine not in (0x14c, 0x8664):
            raise ValueError(f"unsupported machine {machine:#x}; expected i386/AMD64")
        self.bits = 64 if machine == 0x8664 else 32
        self.decoder = cs.Cs(cs.CS_ARCH_X86, cs.CS_MODE_64 if self.bits == 64 else cs.CS_MODE_32)
        self.decoder.detail = True
        self.decoder.skipdata = True
        self.functions = sorted(
            (self.base + e.struct.BeginAddress, self.base + e.struct.EndAddress)
            for e in getattr(self.pe, "DIRECTORY_ENTRY_EXCEPTION", [])
        ) if self.bits == 64 else []
        self.starts = [start for start, _ in self.functions]

    def address(self, value):
        return self.base + int(value[4:], 0) if value.startswith("rva:") else int(value, 0)

    def location(self, va):
        return f"VA {va:#x} RVA {va - self.base:#x}"

    def containing_function(self, va):
        index = bisect.bisect_right(self.starts, va) - 1
        if index >= 0 and va < self.functions[index][1]:
            return self.functions[index]
        return None

    def instructions(self):
        for section in self.pe.sections:
            if section.Characteristics & 0x20000000:  # IMAGE_SCN_MEM_EXECUTE
                yield from self.decoder.disasm(section.get_data()[:section.Misc_VirtualSize],
                                               self.base + section.VirtualAddress)

    def info(self, imports):
        result = {
            "file": self.path.name, "bytes": len(self.data),
            "sha256": hashlib.sha256(self.data).hexdigest(), "bits": self.bits,
            "machine": hex(self.pe.FILE_HEADER.Machine), "preferred_base": hex(self.base),
            "image_size": hex(self.pe.OPTIONAL_HEADER.SizeOfImage),
            "entry_rva": hex(self.pe.OPTIONAL_HEADER.AddressOfEntryPoint),
            "dll_characteristics": hex(self.pe.OPTIONAL_HEADER.DllCharacteristics),
            "runtime_function_entries": len(self.functions),
            "sections": [{"name": s.Name.rstrip(b"\0").decode("ascii", "replace"),
                          "rva": hex(s.VirtualAddress), "virtual_size": hex(s.Misc_VirtualSize),
                          "raw_size": hex(s.SizeOfRawData), "flags": hex(s.Characteristics)}
                         for s in self.pe.sections],
        }
        if imports:
            result["imports"] = {
                d.dll.decode("ascii", "replace"):
                    [{"name": i.name.decode("ascii", "replace") if i.name else f"ordinal:{i.ordinal}",
                      "iat_rva": hex(i.address - self.base)} for i in d.imports]
                for d in getattr(self.pe, "DIRECTORY_ENTRY_IMPORT", [])
            }
        print(json.dumps(result, indent=2))

    def strings(self, pattern, max_length):
        query = re.compile(pattern, re.IGNORECASE)
        # Bound what is displayed: binaries can contain megabytes of base64 shaders.
        for section in self.pe.sections:
            raw = section.get_data()
            matches = []
            for regex, encoding in ((rb"[\x20-\x7e]{5,}", "ascii"),
                                    (rb"(?:[\x20-\x7e]\x00){5,}", "utf-16le")):
                for match in re.finditer(regex, raw):
                    value = match.group().decode(encoding)
                    if len(value) <= max_length and query.search(value):
                        matches.append((match.start(), encoding, value))
            for offset, encoding, value in sorted(matches):
                print(f"{self.location(self.base + section.VirtualAddress + offset)} {encoding}: {value}")

    def xrefs(self, addresses):
        targets = {self.address(a) for a in addresses}
        for insn in self.instructions():
            if not insn.id:  # skip-data pseudo instruction
                continue
            references = set()
            for op in insn.operands:
                if op.type == X86_OP_IMM:
                    references.add(op.imm & ((1 << self.bits) - 1))
                elif op.type == X86_OP_MEM:
                    if op.mem.base == X86_REG_RIP:
                        references.add(insn.address + insn.size + op.mem.disp)
                    elif not op.mem.base and not op.mem.index:
                        references.add(op.mem.disp & ((1 << self.bits) - 1))
            for target in sorted(targets & references):
                function = self.containing_function(insn.address)
                # .pdata describes unwind ranges, which may split a source function;
                # leaf functions may have no entry at all.
                owner = f" unwind_begin_rva={function[0] - self.base:#x}" if function else ""
                print(f"target_rva={target - self.base:#x} {self.location(insn.address)}{owner} "
                      f"{insn.mnemonic} {insn.op_str}")

    def disasm(self, start, end):
        lo, hi = self.address(start), self.address(end)
        if hi <= lo:
            raise ValueError("end must follow start")
        section = self.pe.get_section_by_rva(lo - self.base)
        if section is None or hi > self.base + section.VirtualAddress + min(section.SizeOfRawData,
                                                                           section.Misc_VirtualSize):
            raise ValueError("range must fit within one file-backed section")
        offset = lo - self.base - section.VirtualAddress
        for insn in self.decoder.disasm(section.get_data()[offset:offset + hi - lo], lo):
            print(f"{insn.address:#x} [{insn.address - self.base:#x}] "
                  f"{insn.bytes.hex(' '):<32} {insn.mnemonic} {insn.op_str}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", help="local executable; opened read-only")
    sub = parser.add_subparsers(dest="command", required=True)
    info = sub.add_parser("info", help="fingerprint, architecture, sections, optional imports as JSON")
    info.add_argument("--imports", action="store_true")
    strings = sub.add_parser("strings", help="search bounded ASCII/ASCII-subset UTF-16LE strings")
    strings.add_argument("pattern", help="case-insensitive regular expression")
    strings.add_argument("--max-length", type=int, default=240)
    xrefs = sub.add_parser("xrefs", help="scan executable sections for direct/RIP-relative references")
    xrefs.add_argument("addresses", nargs="+")
    disasm = sub.add_parser("disasm", help="disassemble a file-backed address range")
    disasm.add_argument("start")
    disasm.add_argument("end")
    args = parser.parse_args()
    image = Image(args.exe)
    if args.command == "info":
        image.info(args.imports)
    elif args.command == "strings":
        image.strings(args.pattern, args.max_length)
    elif args.command == "xrefs":
        image.xrefs(args.addresses)
    else:
        image.disasm(args.start, args.end)


if __name__ == "__main__":
    main()
