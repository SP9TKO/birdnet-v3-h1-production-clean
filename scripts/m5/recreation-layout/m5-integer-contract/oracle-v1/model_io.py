"""Metadata-only FlatBuffer reader and scalar-executor package writer."""
import hashlib
import struct
from pathlib import Path

KINDS = {'TRANSPOSE':1,'PAD':2,'CONV_2D':3,'LOGISTIC':4,'MUL':5,'ADD':6,
         'DEPTHWISE_CONV_2D':7,'MEAN':8,'RESHAPE':9,'FULLY_CONNECTED':10}

class View:
    def __init__(self, raw, base):
        self.raw, self.base = raw, base
        self.directory = base - struct.unpack_from('<i', raw, base)[0]
    def location(self, slot):
        size = struct.unpack_from('<H', self.raw, self.directory)[0]
        entry = self.directory + 4 + slot * 2
        if entry >= self.directory + size:
            return None
        delta = struct.unpack_from('<H', self.raw, entry)[0]
        return self.base + delta if delta else None
    def number(self, slot, fmt, default=0):
        p = self.location(slot)
        return struct.unpack_from('<'+fmt, self.raw, p)[0] if p else default
    def pointer(self, slot):
        p = self.location(slot)
        return p + struct.unpack_from('<I', self.raw, p)[0] if p else None
    def child(self, slot):
        p = self.pointer(slot)
        return View(self.raw, p) if p else None
    def numbers(self, slot, fmt):
        p = self.pointer(slot)
        if p is None: return []
        n = struct.unpack_from('<I', self.raw, p)[0]
        return list(struct.unpack_from('<'+fmt*n, self.raw, p+4))
    def children(self, slot):
        p = self.pointer(slot)
        if p is None: return []
        n = struct.unpack_from('<I', self.raw, p)[0]
        return [View(self.raw, p+4+4*k+struct.unpack_from('<I', self.raw, p+4+4*k)[0]) for k in range(n)]
    def bytes(self, slot):
        p = self.pointer(slot)
        if p is None: return b''
        n = struct.unpack_from('<I', self.raw, p)[0]
        return self.raw[p+4:p+4+n]

def source_constants(root, component):
    ident = component['source_identity']
    raw = (root/ident['path']).read_bytes()
    if len(raw)!=ident['bytes'] or hashlib.sha256(raw).hexdigest()!=ident['sha256']:
        raise ValueError('frozen model identity mismatch')
    if raw[4:8]!=b'TFL3': raise ValueError('invalid FlatBuffer identifier')
    model=View(raw,struct.unpack_from('<I',raw)[0]);subgraph,=model.children(2)
    tables=subgraph.children(0);buffers=model.children(4)
    if len(tables)!=len(component['tensors']):raise ValueError('tensor cardinality mismatch')
    result=[]
    for v,t in zip(tables,component['tensors']):
        q=v.child(4)
        bits=[f'0x{x:08x}' for x in q.numbers(2,'I')] if q else []
        points=q.numbers(3,'q') if q else []
        if bits!=t['scale_binary32_bits'] or points!=t['zero_points'] or v.numbers(0,'i')!=t['shape']:
            raise ValueError('frozen tensor metadata mismatch')
        if (q.number(6,'i') if q else 0)!=t['per_axis_dimension']:
            raise ValueError('quantization axis mismatch')
        b=buffers[v.number(2,'I')].bytes(0)
        if len(b)!=t['constant_bytes'] or (b and hashlib.sha256(b).hexdigest()!=t['constant_sha256']):
            raise ValueError('frozen constant identity mismatch')
        result.append((v.number(1,'b'),b))
    if len(subgraph.children(3))!=component['operator_count']:
        raise ValueError('source operation count mismatch')
    return result

def op_parameters(op):
    p=op['parameters'];typ=op['opcode_type']
    if typ in ('CONV_2D','DEPTHWISE_CONV_2D'):
        o=op['builtin_options'];values=[o['strideH'],o['strideW'],o['dilationHFactor'],o['dilationWFactor'],o['padding'],o.get('depthMultiplier',1)]
        return values+[v for q in p['per_channel'] for v in [q['multiplier'],q['right_shift']]]
    if typ=='FULLY_CONNECTED':return [v for q in p['per_channel'] for v in [q['multiplier'],q['right_shift']]]
    if typ in ('MUL','LOGISTIC'):return [p['multiplier'],p['right_shift']]
    if typ=='ADD':return [v for name in ['input1','input2','output'] for v in [p[name]['multiplier'],p[name]['right_shift']]]
    if typ=='MEAN':return [p['adjusted_Q31_multiplier'],p['exponent'],p['divisor']]
    return []

def write_package(root, contract, role, input_bytes, destination):
    c=contract['components'][role];constants=source_constants(root,c)
    ops=[o for o in contract['operators'] if o['component']==role]
    if len(ops)!=c['operator_count']:raise ValueError('frozen operation cardinality mismatch')
    lut=contract['primitive_mathematics']['SIGMOID_LUT_V1']['table_pairs']
    def u32(f,n):f.write(struct.pack('<I',n))
    def u64(f,n):f.write(struct.pack('<Q',n))
    def vector(f,v):
        u32(f,len(v))
        if v:f.write(struct.pack('<'+'I'*len(v),*v))
    with Path(destination).open('xb') as f:
        f.write(b'ORCLV1\0\0');u32(f,len(constants))
        for t,(dtype,raw) in zip(c['tensors'],constants):
            u32(f,dtype);vector(f,t['shape']);u64(f,len(raw));f.write(raw)
        u32(f,len(lut))
        for base,slope in lut:f.write(struct.pack('<qq',base,slope))
        u32(f,len(ops))
        for o in ops:
            u32(f,o['operator_index']);u32(f,KINDS[o['opcode_type']]);vector(f,o['inputs']);u32(f,o['outputs'][0])
            p=op_parameters(o);u32(f,len(p))
            if p:f.write(struct.pack('<'+'q'*len(p),*p))
        input_id,=c['inputs'];u32(f,input_id);u64(f,len(input_bytes)//2);f.write(input_bytes)

def scalar_case_line(case):
    a=case['arguments'];p=case['primitive']
    if p=='high_mul':args=[a['a'],a['multiplier']]
    elif p=='rdpot':args=[a['value'],a['shift']]
    elif p=='clamp16':args=[a['value']]
    elif p=='wrap':args=[a['value'],a['width']]
    elif p=='mean':args=[a['sum'],a['multiplier'],a['exponent']]
    elif p=='rounded_shift':args=[a['value'],a['shift'],a['mode'],a['dbl_rnd']]
    elif p=='mul':args=[a['a'],a['b'],a['multiplier'],a['shift']]
    elif p=='add':args=[a['a'],a['b']]+[v for n in ['input1','input2','output'] for v in [a['parameters'][n]['multiplier'],a['parameters'][n]['right_shift']]]
    elif p=='conv_rescale':args=[a['accumulator'],a['multiplier'],a['shift']]
    elif p=='sigmoid_lut':args=[a['value']]
    elif p=='logistic':args=[a['value'],a['multiplier'],a['shift']]
    elif p=='rational_rne':args=[a['numerator'],a['denominator']]
    elif p=='dot':args=[len(a['inputs'])]+a['inputs']+a['weights']+[a['bias']]
    elif p in ['pad','transpose']:args=[len(a['shape'])]+a['shape']+a['padding' if p=='pad' else 'permutation']+[len(a['values'])]+a['values']
    elif p=='reshape':args=[len(a['values'])]+a['values']
    elif p=='broadcast':args=[len(a['shape'])]+a['shape']+[len(a['input_shape'])]+a['input_shape']+[len(a['values'])]+a['values']
    else:raise ValueError('unimplemented case serialization')
    return case['id']+' '+p+' '+' '.join(str(x) for x in args)
