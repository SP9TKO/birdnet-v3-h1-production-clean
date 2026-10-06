from common import *
from bridge_reference import reference
import ctypes,struct
corpus_path=OUT/'BRIDGE_CONFORMANCE_CORPUS_V1.json';expected_sha=(OUT/'BRIDGE_CONFORMANCE_CORPUS_V1.sha256').read_text().split()[0];assert sha(corpus_path)==expected_sha
corpus=load(corpus_path);checked(load(OUT/'BRIDGE_REFERENCE_IDENTITY.json')['source']);rows=corpus['primitive_cases'];N=len(rows)
lib=ctypes.CDLL(str(OUT/'bridge-host.so'))
lib.bridge_cases.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_int16),ctypes.POINTER(ctypes.c_int8),ctypes.c_size_t];lib.bridge_cases.restype=ctypes.c_int
lib.bridge_vector.argtypes=[ctypes.POINTER(ctypes.c_float),ctypes.POINTER(ctypes.c_int16),ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t),ctypes.POINTER(ctypes.c_size_t)];lib.bridge_vector.restype=ctypes.c_int
src=(ctypes.c_uint32*N)(*(int(r['source_bits'],16) for r in rows));scales=(ctypes.c_uint32*N)(*(int(r['scale_bits'],16) for r in rows));codes=(ctypes.c_int16*N)();sats=(ctypes.c_int8*N)()
assert lib.bridge_cases(src,scales,codes,sats,N)==0
mismatch=0;maxerr=0;sat_errors=0
p=OUT/'primitive-comparisons.jsonl'
assert not p.exists()
with p.open('w') as f:
 for i,r in enumerate(rows):
  ratio,code,saturation=reference(src[i],scales[i]);assert code==r['expected_INT16'];assert str(ratio.numerator)==r['exact_quotient']['numerator'] and str(ratio.denominator)==r['exact_quotient']['denominator']
  err=abs(int(codes[i])-code);mismatch+=err!=0;maxerr=max(maxerr,err);sat_errors+=sats[i]!=saturation
  f.write(json.dumps(dict(index=i,source_bits=r['source_bits'],scale_bits=r['scale_bits'],exact_quotient=r['exact_quotient'],expected_INT16=code,production_INT16=int(codes[i]),equal=err==0,LSB_difference=err,expected_saturation=saturation,production_saturation=int(sats[i])),separators=(',',':'))+'\n')
result=dict(schema='m5-3-bridge-primitive-result-v1',result='PASS' if not mismatch and not sat_errors else 'FAIL',corpus=ident(corpus_path),production_source=ident(WT/'firmware/h1/src/classifier_bridge.cpp'),production_header=ident(WT/'firmware/h1/src/classifier_bridge.hpp'),harness_source=ident(OUT/'bridge_harness.cpp'),harness_library=ident(OUT/'bridge-host.so'),reference=ident(OUT/'bridge_reference.py'),comparisons=ident(p),integer_comparisons=N,TOTAL_BRIDGE_MISMATCHES=mismatch,MAXIMUM_BRIDGE_LSB_ERROR=maxerr,saturation_counter_mismatches=sat_errors,host_flags='-O2 -std=c++17 -fno-fast-math -ffp-contract=off -fsanitize=undefined -fno-sanitize-recover=all')
print(write('BRIDGE_CONFORMANCE_RESULT.json',result));assert result['result']=='PASS','Exact primitive mismatch: stop'
vectors=[];total=0;miss=0;maximum=0
for row in corpus['full_vectors']:
 checked(row['source']);checked(row['accepted_classifier_input']);source=(ROOT/row['source']['path']).read_bytes();bits=struct.unpack('<1280I',source);expected=[reference(b,int(row['scale_bits'],16)) for b in bits]
 x=(ctypes.c_float*1280).from_buffer_copy(source);y=(ctypes.c_int16*1280)();low=ctypes.c_size_t();high=ctypes.c_size_t();assert lib.bridge_vector(x,y,1280,ctypes.byref(low),ctypes.byref(high))==0
 ref_raw=struct.pack('<1280h',*(r[1] for r in expected));prod_raw=struct.pack('<1280h',*y);err=[abs(y[i]-expected[i][1]) for i in range(1280)];n=sum(e!=0 for e in err);maximum=max(maximum,max(err));miss+=n;total+=1280
 assert ref_raw==(ROOT/row['accepted_classifier_input']['path']).read_bytes(),'Retained accepted classifier input differs from frozen policy'
 assert low.value==sum(r[2]<0 for r in expected) and high.value==sum(r[2]>0 for r in expected)
 d=OUT/'vectors'/row['fixture'];d.mkdir(parents=True);(d/'reference.i16le').write_bytes(ref_raw);(d/'production.i16le').write_bytes(prod_raw)
 detail=d/'comparisons.jsonl'
 with detail.open('w') as f:
  for i,(b,(q,c,sat)) in enumerate(zip(bits,expected)):
   f.write(json.dumps(dict(index=i,source_bits=f'0x{b:08x}',scale_bits=row['scale_bits'],exact_quotient={'numerator':str(q.numerator),'denominator':str(q.denominator)},expected_INT16=c,production_INT16=int(y[i]),equal=err[i]==0,LSB_difference=err[i]),separators=(',',':'))+'\n')
 vectors.append(dict(fixture=row['fixture'],source_identity=checked(row['source']),reference_output=ident(d/'reference.i16le'),production_output=ident(d/'production.i16le'),retained_accepted_classifier_input_matches=True,integer_comparisons=1280,TOTAL_MISMATCHES=n,MAXIMUM_LSB_ERROR=max(err),details=ident(detail),saturated_low=low.value,saturated_high=high.value))
full=dict(schema='m5-3-full-vector-bridge-result-v1',result='PASS' if miss==0 else 'FAIL',corpus=ident(corpus_path),production_source=result['production_source'],reference=result['reference'],vector_count=len(vectors),integer_comparisons=total,TOTAL_MISMATCHES=miss,MAXIMUM_LSB_ERROR=maximum,vectors=vectors,scope='Actual production vector bridge; retained accepted M4 V3_host GeM vectors. No physical upstream GeM or integrated inference claim.')
print(write('FULL_VECTOR_BRIDGE_RESULT.json',full));assert full['result']=='PASS','Exact full-vector mismatch: stop'
print(json.dumps(dict(primitive_cases=N,full_vectors=len(vectors),full_vector_integers=total,total_bridge_integers=N+total,TOTAL_BRIDGE_MISMATCHES=mismatch+miss,MAXIMUM_BRIDGE_LSB_ERROR=max(maxerr,maximum)),indent=2))
