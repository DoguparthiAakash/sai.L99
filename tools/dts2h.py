#!/usr/bin/env python3
"""
tools/dts2h.py — sai.L99 devicetree-to-header generator.

Parses the small subset of DTS used by sai.L99 board files and emits a
devicetree.h with:
  - DT_<NODE>_REG_BASE  : register base addresses
  - DT_<NODE>_IRQ       : interrupt numbers
  - DT_<NODE>_LABEL     : string labels
  - DT_CPU_CLOCK_HZ     : first cpu's clock-frequency
  - DT_CHOSEN_CONSOLE   : node id of the chosen console

Only what the BSP needs: node names, reg, interrupts, clock-frequency,
current-speed, label, status, chosen. No phandle resolution beyond
simple single-level references.
"""
import re
import sys

def parse_args():
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} input.dts output.h", file=sys.stderr)
        sys.exit(2)
    return sys.argv[1], sys.argv[2]

def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text

def tokenize_blocks(text, start):
    """Return the {...} block contents starting after 'start {'."""
    depth = 0
    i = start
    while i < len(text):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[start+1:i], i
        i += 1
    return "", i

def parse_node(body, path, nodes):
    """Recursively collect nodes: (path, props, children)."""
    # split into props and child nodes
    children = []
    props = []
    i = 0
    n = len(body)
    while i < n:
        # find next node start or property
        m = re.search(r'([A-Za-z0-9_,.\-]+)\s*:\s*([A-Za-z0-9_,\-]+)@?([0-9a-fA-F]*)\s*\{', body[i:])
        pm = re.search(r'([A-Za-z0-9_,\-]+)\s*[:=]', body[i:])
        if m and (not pm or m.start() <= pm.start()):
            # child node
            label = m.group(1)
            name = m.group(2)
            unit = m.group(3)
            brace = i + m.end() - 1
            child_body, end = tokenize_blocks(body, brace)
            full = f"{path}/{name}" + (f"@{unit}" if unit else "")
            nodes.append({
                'label': label,
                'name': name,
                'unit': unit,
                'path': full,
                'body': child_body,
            })
            parse_node(child_body, full, nodes)
            i = end + 1
        elif pm:
            # property line: name = value; or name;
            line_end = body.find(';', i)
            if line_end == -1:
                break
            line = body[i:line_end].strip()
            if '=' in line:
                k, v = line.split('=', 1)
                props.append((k.strip(), v.strip()))
            i = line_end + 1
        else:
            i += 1
    return props

def to_c(name):
    name = re.sub(r'[^A-Za-z0-9]', '_', name.upper())
    return re.sub(r'_+', '_', name).strip('_')

def cells_values(val):
    """Extract numeric cells and strings from a dts property value."""
    vals = []
    for m in re.finditer(r'<([^>]*)>|&([A-Za-z0-9_]+)|"([^"]*)"', val):
        if m.group(1) is not None:
            for tok in m.group(1).split():
                if tok.startswith('0x'):
                    vals.append(('u32', int(tok, 16)))
                else:
                    vals.append(('u32', int(tok, 0)))
        elif m.group(2) is not None:
            vals.append(('ref', m.group(2)))
        elif m.group(3) is not None:
            vals.append(('str', m.group(3)))
    return vals

def main():
    src, dst = parse_args()
    text = strip_comments(open(src).read())

    nodes = []
    root_body, _ = tokenize_blocks(text, text.find('/ {') + 2)
    parse_node(root_body, "", nodes)

    lines = []
    lines.append("/** @file devicetree.h @brief GENERATED from %s - do not edit. */" % src)
    lines.append("#ifndef SAI_DEVICETREE_H")
    lines.append("#define SAI_DEVICETREE_H")
    lines.append("")

    node_ids = {}
    next_id = 1
    for nd in nodes:
        node_ids[nd['label']] = next_id
        lines.append(f"#define DT_{to_c(nd['name'])}_NODE_ID {next_id}")
        next_id += 1
    lines.append("")

    for nd in nodes:
        prefix = f"DT_{to_c(nd['name'])}"
        props = dict()
        # pull top-level properties of this node from its body
        for m in re.finditer(r'([A-Za-z0-9_,\-]+)\s*=\s*([^;]+);', nd['body']):
            props[m.group(1)] = m.group(2).strip()
        reg = props.get('reg', '')
        if reg:
            vals = cells_values(reg)
            if vals:
                lines.append(f"#define {prefix}_REG_BASE 0x{vals[0][1]:08X}u")
                if len(vals) > 1:
                    lines.append(f"#define {prefix}_REG_SIZE 0x{vals[1][1]:X}u")
        irq = props.get('interrupts', '')
        if irq:
            vals = cells_values(irq)
            if vals:
                lines.append(f"#define {prefix}_IRQ {vals[0][1]}u")
        for p in ('clock-frequency', 'current-speed', 'label', 'status'):
            if p in props:
                v = props[p]
                sv = cells_values(v)
                cname = to_c(p)
                if sv and sv[0][0] == 'u32':
                    lines.append(f"#define {prefix}_{cname} {sv[0][1]}u")
                elif sv and sv[0][0] == 'str':
                    lines.append(f'#define {prefix}_{cname} "{sv[0][1]}"')
        lines.append("")

    # chosen console
    m = re.search(r'sai,console\s*=\s*&([A-Za-z0-9_]+)', text)
    if m:
        label = m.group(1)
        lines.append(f"#define DT_CHOSEN_CONSOLE_NODE_ID {node_ids.get(label, 0)}")
    m = re.search(r'clock-frequency\s*=\s*<\s*(\d+)\s*>', text)
    if m:
        lines.append(f"#define DT_CPU_CLOCK_HZ {m.group(1)}u")

    lines.append("")
    lines.append("#endif /* SAI_DEVICETREE_H */")
    lines.append("")

    open(dst, 'w').write("\n".join(lines))
    print(f"dts2h: wrote {dst} ({len(nodes)} nodes)")

if __name__ == '__main__':
    main()
