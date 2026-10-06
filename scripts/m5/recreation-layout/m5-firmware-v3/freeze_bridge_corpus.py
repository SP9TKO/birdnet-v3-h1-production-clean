from common import *
from bridge_reference import binary32,reference
from fractions import Fraction
import math,struct,random
assert not (OUT/'BRIDGE_CONFORMANCE_CORPUS_V1.json').exists()
# Scope-limited search performed before selecting this new corpus.
retained=[str(p.relative_to(ROOT)) for p in (ROOT/'.development-work').rglob('*BRIDGE*CORPUS*') if 'm5-firmware-v3' not in str(p)]
assert not retained,retained
cases={}
def add(bits,scale,reason):
 assert (bits&0x7f800000)!=0x7f800000
 key=(bits,scale)
 if key not in cases:cases[key]=dict(source_bits=f'0x{bits:08x}',scale_bits=f'0x{scale:08x}',construction=reason)
def nearest_bits(x):return struct.unpack('<I',struct.pack('<f',float(x)))[0]
S=0x380049c8;s=binary32(S)
# Every in-range half-decision and two decisions beyond each saturation endpoint.
# Exact scale*half has <=41 significand bits here, so float(x) is exact binary64.
for lower in range(0,32771):
 decision=s*Fraction(2*lower+1,2)
 assert binary32(nearest_bits(decision))!=0
 assert Fraction.from_float(float(decision))==decision
 b=nearest_bits(decision)
 for neighbor in (b-1,b,b+1):
  for sign in (0,0x80000000):add(neighbor|sign,S,'binary32 neighbor at decision '+str(lower)+'+1/2, both signs')
for val in [Fraction(0),s/4,s/2,s,s*2,s*3,s*7,s*16,s*32766,s*32767,s*32768,Fraction(1,1000000),Fraction(1,10),Fraction(1),Fraction(7,4),Fraction(2),Fraction(16)]:
 b=nearest_bits(val)
 for sign in (0,0x80000000):add(b|sign,S,'zero/integer quotient/saturation/representative GeM magnitude')
for scale in [S,0x3f800000,0x3f000000,0x3f400000,1,0x007fffff,0x00800000,0x7f7fffff]:
 for exponent in range(255):
  for mantissa in [0,1,0x3fffff,0x7fffff]:
   for sign in [0,0x80000000]:add(sign|(exponent<<23)|mantissa,scale,'exponent/significand sweep including subnormals and max finite')
 for n in [0,1,2,3,7,15,31,32766,32767,32768]:
  x=binary32(scale)*Fraction(2*n+1,2)
  if x>binary32(0x7f7fffff):continue
  b=nearest_bits(x)
  for neighbor in [max(0,b-1),b,min(0x7f7fffff,b+1)]:
   for sign in [0,0x80000000]:add(sign|neighbor,scale,'synthetic scale half-tie/ULP boundary')
rng=random.Random(0x4d353342)
for _ in range(10000):
 b=rng.getrandbits(32)
 if (b&0x7f800000)!=0x7f800000:add(b,S,'prospective fixed-seed finite binary32 sampling')
manifest=load(ROOT/'.development-work/m5-integer-contract/M5_2_FORMAL_CAMPAIGN_MANIFEST_V1_ROUTE_EXTENDED.prepared.json')
num=load(ROOT/'.development-work/m4-shared-feature-closure/M4_V3_HOST_NUMERICAL_RESULT.json');assert num['result']=='PASS' and num['fixture_count']==38
by_name={r['fixture']:r for r in num['rows']};vectors=[]
for fixture in manifest['inputs']:
 r=by_name[fixture['fixture']];emb=r['new_vectors']['embedding'];checked(emb)
 assert emb['bytes']==5120 and emb['shape']==[1,1280] and emb['dtype']=='float32'
 checked(r['new_vectors']['classifier_input']);assert r['new_vectors']['classifier_input']['sha256']==fixture['component_stage_inputs']['classifier_input']['sha256']
 vectors.append(dict(fixture=fixture['fixture'],source=emb,scale_bits=f'0x{S:08x}',upstream_source='Accepted M4 V3_host dequantized shared-feature -> unchanged FP32 GeM TFLite embedding; not physical M55 GeM equivalence.',authority=ident(ROOT/'.development-work/m4-shared-feature-closure/M4_V3_HOST_NUMERICAL_RESULT.json'),accepted_classifier_input=r['new_vectors']['classifier_input']))
rows=[]
for (bits,scale),row in sorted(cases.items()):
 quotient,code,sat=reference(bits,scale)
 row.update(exact_quotient={'numerator':str(quotient.numerator),'denominator':str(quotient.denominator)},expected_INT16=code,expected_saturation=sat)
 rows.append(row)
ref=write('BRIDGE_REFERENCE_IDENTITY.json',dict(source=ident(OUT/'bridge_reference.py'),algorithm='Exact IEEE binary32 integer decomposition; Python Fraction quotient; signed floor and independently implemented nearest-even; post-round INT16 clamp.',independence='Does not import/call production bridge or frozen 550-op M5-2 oracle.',policy=ident(ROOT/'.development-work/m5-integer-contract/PROSPECTIVE_BRIDGE_POLICY_V1.json')))
corpus=write('BRIDGE_CONFORMANCE_CORPUS_V1.json',dict(schema='m5-3-bridge-conformance-corpus-v1',state='PROSPECTIVELY_FROZEN_BEFORE_PRODUCTION_COMPARISON',retained_prior_M5_3_corpus_found=False,production_bridge_outputs_observed=False,policy=ident(ROOT/'.development-work/m5-integer-contract/PROSPECTIVE_BRIDGE_POLICY_V1.json'),reference=ref,primitive_case_count=len(rows),primitive_cases=rows,full_vector_count=len(vectors),full_vector_elements=48640,full_vectors=vectors,corpus_selection='All production-scale INT16 half decisions and ULP neighbors, both signs; fixed exponent/significand and scale sweeps; fixed random seed; all 38 accepted GeM embeddings. No production result used for selection.'))
(OUT/'BRIDGE_CONFORMANCE_CORPUS_V1.sha256').write_text(corpus['sha256']+'  BRIDGE_CONFORMANCE_CORPUS_V1.json\n')
print(json.dumps(dict(corpus=corpus,primitive_count=len(rows),full_vector_count=len(vectors),reference=ident(OUT/'bridge_reference.py')),indent=2))
